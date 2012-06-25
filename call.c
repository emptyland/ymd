#include "compiler.h"
#include "value.h"
#include "state.h"
#include "memory.h"
#include "assembly.h"
#include "encode.h"
#include "3rd/regex/regex.h"
#include <stdio.h>

// +----------------------+
// |  bind  |    local    |
// +--------+-------------+
// | n_bind |n_lz - n_bind|
// +----------------------+
static inline struct variable *vm_local(struct func *fn, int i) {
	struct context *l = ioslate();
	assert(fn == l->info->run);
	if (i < fn->n_bind)
		return fn->bind + i;
	return l->info->loc + i - fn->n_bind;
}

static inline struct variable *vm_find_local(struct func *fn,
                                             const char *name) {
	int i = blk_find_lz(fn->u.core, name);
	if (i < 0)
		vm_die("Can not find local: %s", name);
	return vm_local(fn, i);
}

static inline struct variable *vm_global(struct func *fn, int i) {
	struct kstr *k;
	struct variable key;
	struct chunk *core = fn->u.core;
	assert(i >= 0);
	assert(i < core->kkz);
	k = core->kz[i];
	key.type = T_KSTR;
	key.value.ref = gcx(k);
	return hmap_get(vm()->global, &key);
}

static inline int vm_match(const struct kstr *lhs, const struct kstr *pattern) {
	regex_t regex;
	int err = regcomp(&regex, pattern->land, REG_NOSUB|REG_EXTENDED);
	if (err) {
		char msg[128];
		regerror(err, &regex, msg, sizeof(msg));
		regfree(&regex);
		vm_die("Regex match fatal: %s", msg);
		return 0;
	}
	err = regexec(&regex, lhs->land, 0, NULL, 0);
	regfree(&regex);
	return !err;
}

static inline int vm_bool(const struct variable *lhs) {
	switch (lhs->type) {
	case T_NIL:
		return 0;
	case T_BOOL:
		return lhs->value.i;
	default:
		return 1;
	}
	return 0;
}

static inline void do_put(struct gc_node *raw, const struct variable *k,
                          const struct variable *v) {
	if (is_nil(k) || is_nil(v))
		vm_die("Value can not be `nil` in k-v pair");
	switch (raw->type) {
	case T_HMAP:
		*hmap_put((struct hmap *)raw, k) = *v;
		break;
	case T_SKLS:
		*skls_put((struct skls *)raw, k) = *v;
		break;
	case T_DYAY:
		*dyay_add((struct dyay *)raw) = *v;
	default:
		assert(0);
		break;
	}
}

static inline void vm_put(struct variable *var, const struct variable *k,
                   const struct variable *v) {
	struct variable *rv = ymd_put(var, k);
	if (is_nil(v))
		vm_die("Value can not be `nil` in k-v pair");
	*rv = *v;
}

#define IMPL_JUMP          \
	switch (flag) {        \
	case F_FORWARD:        \
		info->pc += param; \
		break;             \
	case F_BACKWARD:       \
		info->pc -= param; \
		break;             \
	default:               \
		assert(0);         \
		break;             \
	}
//-----------------------------------------------------------------------------
// Stack functions:
// ----------------------------------------------------------------------------
int vm_run(struct func *fn, int argc) {
	uint_t inst;
	struct context *l = ioslate();
	struct call_info *info = l->info;
	struct chunk *core = fn->u.core;

	(void)argc;
	assert(fn == info->run);

retry:
	while (info->pc < core->kinst) {
		inst = core->inst[info->pc];
		uchar_t op = asm_op(inst);
		uchar_t flag = asm_flag(inst);
		ushort_t param = asm_param(inst);
		switch (op) {
		case I_PANIC:
			vm_die("%s Panic!", fn->proto->land);
			break;
		case I_STORE:
			switch (flag) {
			case F_LOCAL:
				*vm_local(fn, param) = *ymd_top(l, 0);
				break;
			case F_OFF:
				*vm_global(fn, param) = *ymd_top(l, 0);
				break;
			}
			ymd_pop(l, 1);
			break;  
		case I_RET:
			return param; // return!
		case I_JNE:
			if (vm_bool(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
			IMPL_JUMP
			ymd_pop(l, 1);
			goto retry;
		case I_JNT:
			if (vm_bool(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
			IMPL_JUMP
			goto retry;
		case I_JNN:
			if (!vm_bool(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
			IMPL_JUMP
			goto retry;
		case I_JMP:
			IMPL_JUMP
			goto retry;
		case I_FOREACH: {
			struct variable *cond = ymd_top(l, 0);
			if (cond->type == T_EXT &&
				cond->value.ext == (void *)-1) { // FIXME: use a macro or constant
				switch (flag) {
				case F_FORWARD:
					info->pc += param;
					break;
				case F_BACKWARD:
					info->pc -= param;
					break;
				default:
					assert(0);
					break;
				}
				ymd_pop(l, 1);
				goto retry;
			}
			} break;
		case I_SETF: {
			switch (flag) {
			case F_STACK: {
				int i, k = param << 1;
				struct variable *var = ymd_top(l, k);
				for (i = 0; i < k; i += 2)
					vm_put(var, ymd_top(l, i + 1), ymd_top(l, i));
				ymd_pop(l, k + 1);
				} break;
			case F_FAST: {
				struct variable key;
				vset_kstr(&key, core->kz[param]);
				vm_put(ymd_top(l, 1), &key, ymd_top(l, 0));
				ymd_pop(l, 2);
				} break;
			default:
				assert(0);
				break;
			}
			} break;
		case I_PUSH: {
			struct variable var;
			switch (flag) {
			case F_INT:
				var.type = T_INT;
				var.value.i = zigzag_decode(param);
				break;
			case F_PARTAL: {
				ushort_t partal[MAX_VARINT16_LEN];
				int i = 0;
				while (asm_flag(inst) != F_INT) {
					inst = core->inst[info->pc];
					assert(asm_op(inst) == I_PUSH);
					partal[i++] = asm_param(inst);
					++info->pc;
				}
				var.type = T_INT;
				var.value.i = varint16_decode(partal, i);
				--info->pc;
				} break;
			case F_ZSTR:
				var.type = T_KSTR;
				var.value.ref = gcx(core->kz[param]);
				break;
			case F_LOCAL:
				var = *vm_local(fn, param);
				break;
			case F_BOOL:
				var.type = T_BOOL;
				var.value.i = param;
				break;
			case F_NIL:
				var.type = T_NIL;
				var.value.i = 0;
				break;
			case F_OFF:
				var = *vm_global(fn, param);
				break;
			}
			*ymd_push(l) = var;
			} break;
		case I_TEST: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			switch (flag) {
			case F_EQ:
				rhs->value.i = equals(rhs, lhs);
				break;
			case F_NE:
				rhs->value.i = !equals(rhs, lhs);
				break;
			case F_GT:
				rhs->value.i = compare(rhs, lhs) > 0;
				break;
			case F_GE:
				rhs->value.i = compare(rhs, lhs) >= 0;
				break;
			case F_LT:
				rhs->value.i = compare(rhs, lhs) < 0;
				break;
			case F_LE:
				rhs->value.i = compare(rhs, lhs) <= 0;
				break;
			case F_MATCH:
				rhs->value.i = vm_match(kstr_of(rhs), kstr_of(lhs));
				break;
			default:
				assert(0);
				break;
			}
			rhs->type = T_BOOL;
			ymd_pop(l, 1);
			} break;
		case I_NOT: {
			struct variable *opd = ymd_top(l, 0);
			vset_bool(opd, !vm_bool(opd));
			} break;
		case I_GETF: {
			switch (flag) {
			case F_STACK: {
				struct variable *k = ymd_top(l, 0),
								*v = ymd_get(ymd_top(l, 1), k);
				ymd_pop(l, 2);
				*ymd_push(l) = *v;
				} break;
			case F_FAST: {
				struct variable k;
				vset_kstr(&k, core->kz[param]);
				*ymd_top(l, 0) = *ymd_get(ymd_top(l, 0), &k);
				} break;
			default:
				assert(0);
				break;
			}
			} break;
		case I_TYPEOF: {
			const unsigned tt = ymd_top(l, 0)->type;
			struct kstr *kz;
			assert(tt < T_MAX);
			kz = typeof_kstr(tt);
			ymd_top(l, 0)->type = T_KSTR;
			ymd_top(l, 0)->value.ref = gcx(kz);
			} break;
		case I_INV: {
			struct variable *opd = ymd_top(l, 0);
			opd->value.i = -int_of(opd);
			} break;
		case I_MUL: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(rhs) * int_of(lhs);
			ymd_pop(l, 1);
			} break;
		case I_DIV: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			ymd_int_t opd2 = int_of(lhs);
			if (opd2 == 0LL)
				vm_die("Div to zero");
			rhs->value.i = int_of(rhs) / opd2;
			ymd_pop(l, 1);
			} break;
		case I_ADD: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			switch (rhs->type) {
			case T_INT:
				rhs->value.i = rhs->value.i + int_of(lhs);
				break;
			case T_KSTR:
				rhs->value.ref = gcx(ymd_strcat(kstr_of(rhs),
					kstr_of(lhs)));
				break;
			default:
				vm_die("Operator + don't support this type");
				break;
			}
			ymd_pop(l, 1);
			} break;
		case I_SUB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(rhs) - int_of(lhs);
			ymd_pop(l, 1);
			} break;
		case I_MOD: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			ymd_int_t opd1 = int_of(lhs);
			if (opd1 == 0LL)
				vm_die("Mod to zero");
			rhs->value.i = int_of(rhs) % int_of(lhs);
			ymd_pop(l, 1);
			} break;
		case I_POW: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			ymd_int_t opd0 = int_of(rhs), opd1 = int_of(lhs);
			if (opd1 < 0) {
				vm_die("Pow must be great or equals 0");
			} else if (opd1 == 0) {
				rhs->value.i = 1;
			} else if (opd0 == 1) {
				rhs->value.i = 1;
			} else if (opd0 == 0) {
				rhs->value.i = 0;
			} else { 
				--opd1;
				while (opd1--)
					rhs->value.i *= opd0;
			}
			ymd_pop(l, 1);
			} break;
		case I_ANDB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(rhs) & int_of(lhs);
			ymd_pop(l, 1);
			} break;
		case I_ORB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(rhs) | int_of(lhs);
			ymd_pop(l, 1);
			} break;
		case I_XORB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(rhs) ^ int_of(lhs);
			ymd_pop(l, 1);
			} break;
		case I_INVB: {
			struct variable *opd0 = ymd_top(l, 0);
			opd0->value.i = ~opd0->value.i;
			} break;
		case I_SHIFT: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			if (int_of(lhs) < 0)
				vm_die("Shift must be great than 0");
			switch (flag) {
			case F_LEFT:
				rhs->value.i = int_of(rhs) << int_of(lhs);
				break;
			case F_RIGHT_L:
				rhs->value.i = ((unsigned long long)int_of(rhs)) >> int_of(lhs);
				break;
			case F_RIGHT_A:
				rhs->value.i = int_of(rhs) >> int_of(lhs);
				break;
			default:
				assert(0);
				break;
			}
			ymd_pop(l, 1);
			} break;
		case I_CALL: {
			struct func *called;
			int argc = param, nrt = 0;
			called = func_of(ymd_top(l, argc));
			nrt = func_call(called, argc);
			} break;
		case I_NEWMAP: {
			struct hmap *map = hmap_new(param);
			int i, n = param * 2;
			for (i = 0; i < n; i += 2)
				do_put(gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			vset_hmap(ymd_push(l), map);
			} break;
		case I_NEWSKL: {
			struct skls *map = skls_new();
			int i, n = param * 2;
			for (i = 0; i < n; i += 2)
				do_put(gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			vset_skls(ymd_push(l), map);
			} break;
		case I_NEWDYA: {
			struct dyay *map = dyay_new(0);
			int i = param;
			while (i--)
				do_put(gcx(map), NULL, ymd_top(l, i));
			ymd_pop(l, param);
			vset_dyay(ymd_push(l), map);
			} break;
		case I_BIND: {
			struct variable *opd = ymd_top(l, param);
			struct func *copied = func_clone(func_of(opd));
			int i = param;
			assert(copied->n_bind == param);
			while (i--)
				func_bind(copied, param - i - 1, ymd_top(l, i));
			opd->value.ref = gcx(copied); // Copy-on-wirte bind
			ymd_pop(l, param);
			} break;
		case I_LOAD: {
			struct variable *var = ymd_push(l);
			struct func *land = vm()->fn[param];
			var->type = T_FUNC;
			var->value.ref = gcx(land);
			} break;
		default:
			assert(0);
			break;
		}
		++info->pc;
	}
	return 0;
}
#undef IMPL_JUMP

static void copy_args(struct func *fn, int argc) {
	int i;
	struct variable *argv = NULL;
	struct context *l = ioslate();
	if (!fn->is_c) {
		struct chunk *core = fn->u.core;
		const int k = core->kargs < argc ? core->kargs : argc;
		argv = vm_find_local(fn, "argv");
		vset_nil(argv);
		i = k;
		while (i--) // Copy to local variable
			*vm_local(fn, k - i - 1) = *ymd_top(l, i);
	}
	if (argc > 0) {
		// Lazy creating
		fn->argv = !fn->argv ? dyay_new(0) : fn->argv;
		i = argc;
		while (i--) // Copy to array: argv
			*dyay_add(fn->argv) = *ymd_top(l, i);
		if (!fn->is_c)
			vset_dyay(argv, fn->argv);
	}
}

int func_call(struct func *fn, int argc) {
	int rv;
	struct call_info scope;
	struct context *l = ioslate();
	int main_fn = (l->info == NULL);

	// Initialize local variable
	if (main_fn) {
		scope.loc = l->loc;
	} else {
		int n = func_nlocal(l->info->run);
		scope.loc = l->info->loc + n;
		memset(scope.loc, 0, n * sizeof(*scope.loc));
	}
	// Link the call info
	scope.pc = 0;
	scope.run = fn;
	scope.chain = l->info;
	l->info = &scope;
	copy_args(fn, argc);
	// Pop all args
	ymd_pop(l, argc + 1);
	if (fn->is_c) {
		rv = (*fn->u.nafn)(ioslate());
		goto ret;
	}
	rv = vm_run(fn, argc);
ret:
	if (fn->argv)
		dyay_final(fn->argv);
	l->info = l->info->chain;
	return rv;
}

int func_main(struct func *fn, int argc, char *argv[]) {
	int i;
	struct context *l = ioslate();
	ymd_push_func(l, fn);
	for (i = 0; i < argc; ++i)
		ymd_push_kstr(l, argv[i], -1);
	if (setjmp(l->jpt)) {
		return -1;
	}
	return func_call(fn, argc);
}

