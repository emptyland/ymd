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
static inline struct variable *vm_local(struct ymd_context *l,
                                        struct func *fn, int i) {
	assert(fn == l->info->run);
	if (i < fn->n_bind)
		return fn->bind + i;
	return l->info->loc + i - fn->n_bind;
}

static inline struct variable *vm_find_local(struct ymd_context *l,
                                             struct func *fn,
                                             const char *name) {
	int i = blk_find_lz(fn->u.core, name);
	if (i < 0)
		vm_die(l->vm, "Can not find local: %s", name);
	return vm_local(l, fn, i);
}

static inline const struct variable *do_keyz(struct func *fn, int i,
                                             struct variable *key) {
	struct kstr *k;
	struct chunk *core = fn->u.core;
	assert(i >= 0);
	assert(i < core->kkz);
	k = core->kz[i];
	key->type = T_KSTR;
	key->value.ref = gcx(k);
	return key;
}

static inline struct variable *vm_igetg(struct ymd_mach *vm, int i) {
	struct variable k;
	struct func *fn = ymd_called(ioslate(vm));
	return hmap_get(vm->global, do_keyz(fn, i, &k));
}

static inline void vm_iputg(struct ymd_mach *vm, int i, const struct variable *v) {
	struct variable k;
	struct func *fn = ymd_called(ioslate(vm));
	*hmap_put(vm, vm->global, do_keyz(fn, i, &k)) = *v;
}

static inline int vm_match(struct ymd_mach *vm,
                           const struct kstr *lhs,
                           const struct kstr *pattern) {
	regex_t regex;
	int err = regcomp(&regex, pattern->land, REG_NOSUB|REG_EXTENDED);
	if (err) {
		char msg[128];
		regerror(err, &regex, msg, sizeof(msg));
		regfree(&regex);
		vm_die(vm, "Regex match fatal: %s", msg);
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

static inline void do_put(struct ymd_mach *vm,
                          struct gc_node *raw,
						  const struct variable *k,
                          const struct variable *v) {
	if ((k && is_nil(k)) || is_nil(v))
		vm_die(vm, "Value can not be `nil` in k-v pair");
	switch (raw->type) {
	case T_HMAP:
		*hmap_put(vm, (struct hmap *)raw, k) = *v;
		break;
	case T_SKLS:
		*skls_put(vm, (struct skls *)raw, k) = *v;
		break;
	case T_DYAY:
		*dyay_add(vm, (struct dyay *)raw) = *v;
		break;
	default:
		assert(0);
		break;
	}
}

static inline void vm_iput(struct ymd_mach *vm,
                          struct variable *var,
						  const struct variable *k,
                          const struct variable *v) {
	struct variable *rv = vm_put(vm, var, k);
	if (is_nil(v))
		vm_die(vm, "Value can not be `nil` in k-v pair");
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
int vm_run(struct ymd_context *l, struct func *fn, int argc) {
	uint_t inst;
	struct call_info *info = l->info;
	struct chunk *core = fn->u.core;
	struct ymd_mach *vm = l->vm;

	(void)argc;
	assert(fn == info->run);

retry:
	while (info->pc < core->kinst) {
		inst = core->inst[info->pc];
		uchar_t op = asm_op(inst);
		uchar_t flag = asm_flag(inst);
		ushort_t param = asm_param(inst);
		l->vm->tick++;
		switch (op) {
		case I_PANIC:
			vm_die(vm, "%s Panic!", fn->proto->land);
			break;
		case I_STORE:
			switch (flag) {
			case F_LOCAL:
				*vm_local(l, fn, param) = *ymd_top(l, 0);
				break;
			case F_OFF:
				vm_iputg(vm, param, ymd_top(l, 0));
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
			if (is_nil(ymd_top(l, 0))) {
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
					vm_iput(vm, var, ymd_top(l, i + 1), ymd_top(l, i));
				ymd_pop(l, k + 1);
				} break;
			case F_FAST: {
				struct variable key;
				vset_kstr(&key, core->kz[param]);
				vm_iput(vm, ymd_top(l, 1), &key, ymd_top(l, 0));
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
				ushort_t partial[MAX_VARINT16_LEN];
				int i = 0;
				while (asm_flag(inst) != F_INT) {
					inst = core->inst[info->pc];
					assert(asm_op(inst) == I_PUSH);
					partial[i++] = asm_param(inst);
					++info->pc;
				}
				var.type = T_INT;
				var.value.i = varint16_decode(partial, i);
				--info->pc;
				} break;
			case F_ZSTR:
				var.type = T_KSTR;
				var.value.ref = gcx(core->kz[param]);
				break;
			case F_LOCAL:
				var = *vm_local(l, fn, param);
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
				var = *vm_igetg(vm, param);
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
				rhs->value.i = vm_match(vm, kstr_of(vm, rhs), kstr_of(vm, lhs));
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
								*v = vm_get(vm, ymd_top(l, 1), k);
				ymd_pop(l, 2);
				*ymd_push(l) = *v;
				} break;
			case F_FAST: {
				struct variable k;
				vset_kstr(&k, core->kz[param]);
				*ymd_top(l, 0) = *vm_get(vm, ymd_top(l, 0), &k);
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
			kz = typeof_kstr(vm, tt);
			ymd_top(l, 0)->type = T_KSTR;
			ymd_top(l, 0)->value.ref = gcx(kz);
			} break;
		case I_INV: {
			struct variable *opd = ymd_top(l, 0);
			opd->value.i = -int_of(vm, opd);
			} break;
		case I_MUL: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(vm, rhs) * int_of(vm, lhs);
			ymd_pop(l, 1);
			} break;
		case I_DIV: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			ymd_int_t opd2 = int_of(vm, lhs);
			if (opd2 == 0LL)
				vm_die(vm, "Div to zero");
			rhs->value.i = int_of(vm, rhs) / opd2;
			ymd_pop(l, 1);
			} break;
		case I_ADD: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			switch (rhs->type) {
			case T_INT:
				rhs->value.i = rhs->value.i + int_of(vm, lhs);
				break;
			case T_KSTR:
				rhs->value.ref = gcx(vm_strcat(vm, kstr_of(vm, rhs),
				                                kstr_of(vm, lhs)));
				break;
			default:
				vm_die(vm, "Operator + don't support this type");
				break;
			}
			ymd_pop(l, 1);
			} break;
		case I_SUB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(vm, rhs) - int_of(vm, lhs);
			ymd_pop(l, 1);
			} break;
		case I_MOD: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			ymd_int_t opd1 = int_of(vm, lhs);
			if (opd1 == 0LL)
				vm_die(vm, "Mod to zero");
			rhs->value.i = int_of(vm, rhs) % int_of(vm, lhs);
			ymd_pop(l, 1);
			} break;
		case I_POW: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			ymd_int_t opd0 = int_of(vm, rhs), opd1 = int_of(vm, lhs);
			if (opd1 < 0) {
				vm_die(vm, "Pow must be great or equals 0");
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
			rhs->value.i = int_of(vm, rhs) & int_of(vm, lhs);
			ymd_pop(l, 1);
			} break;
		case I_ORB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(vm, rhs) | int_of(vm, lhs);
			ymd_pop(l, 1);
			} break;
		case I_XORB: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			rhs->value.i = int_of(vm, rhs) ^ int_of(vm, lhs);
			ymd_pop(l, 1);
			} break;
		case I_INVB: {
			struct variable *opd0 = ymd_top(l, 0);
			opd0->value.i = ~opd0->value.i;
			} break;
		case I_SHIFT: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			if (int_of(vm, lhs) < 0)
				vm_die(vm, "Shift must be great than 0");
			switch (flag) {
			case F_LEFT:
				rhs->value.i = int_of(vm, rhs) << int_of(vm, lhs);
				break;
			case F_RIGHT_L:
				rhs->value.i = ((unsigned long long)int_of(vm, rhs)) >> int_of(vm, lhs);
				break;
			case F_RIGHT_A:
				rhs->value.i = int_of(vm, rhs) >> int_of(vm, lhs);
				break;
			default:
				assert(0);
				break;
			}
			ymd_pop(l, 1);
			} break;
		case I_CALL: {
			int adjust = asm_aret(inst);
			int argc = asm_argc(inst);
			struct func *called = func_of(vm, ymd_top(l, argc));
			ymd_adjust(l, adjust, ymd_call(l, called, argc, 0));
			} break;
		// flag  : argc
		// param : method name
		case I_SELFCALL: {
			struct variable method;
			struct func *called;
			int adjust = asm_aret(inst);
			int argc = asm_argc(inst);
			int imethod = asm_method(inst);
			vset_kstr(&method, fn->u.core->kz[imethod]);
			called = func_of(vm, vm_get(vm, ymd_top(l, argc), &method));
			ymd_adjust(l, adjust, ymd_call(l, called, argc, 1));
			} break;
		case I_NEWMAP: {
			struct hmap *map = hmap_new(vm, param);
			int i, n = param * 2;
			for (i = 0; i < n; i += 2)
				do_put(vm, gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			map->marked = 0;
			vset_hmap(ymd_push(l), map);
			} break;
		case I_NEWSKL: {
			struct skls *map = skls_new(vm);
			int i, n = param * 2;
			for (i = 0; i < n; i += 2)
				do_put(vm, gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			map->marked = 0;
			vset_skls(ymd_push(l), map);
			} break;
		case I_NEWDYA: {
			struct dyay *map = dyay_new(vm, 0);
			int i = param;
			while (i--)
				do_put(vm, gcx(map), NULL, ymd_top(l, i));
			ymd_pop(l, param);
			map->marked = 0;
			vset_dyay(ymd_push(l), map);
			} break;
		case I_BIND: {
			struct variable *opd = ymd_top(l, param);
			struct func *copied = func_clone(vm, func_of(vm, opd));
			int i = param;
			assert(copied->n_bind == param);
			while (i--)
				func_bind(vm, copied, param - i - 1, ymd_top(l, i));
			copied->marked = 0;
			opd->value.ref = gcx(copied); // Copy-on-wirte bind
			ymd_pop(l, param);
			} break;
		case I_LOAD: {
			struct variable *var = ymd_push(l);
			struct func *land = vm->fn[param];
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

static void vm_copy_args(struct ymd_context *l, struct func *fn, int argc) {
	int i;
	struct variable *argv = NULL;
	if (!fn->is_c) {
		struct chunk *core = fn->u.core;
		const int k = core->kargs < argc ? core->kargs : argc;
		argv = vm_find_local(l, fn, "argv");
		vset_nil(argv);
		i = k;
		while (i--) // Copy to local variable
			//*vm_local(l, fn, k - i - 1) = *ymd_top(l, i);
			l->info->loc[k - i - 1] = *ymd_top(l, i);
	}
	if (argc > 0) {
		// Lazy creating
		fn->argv = !fn->argv ? dyay_new(l->vm, argc) : fn->argv;
		fn->argv->marked = 0;
		i = argc;
		while (i--) // Copy to array: argv
			*dyay_add(l->vm, fn->argv) = *ymd_top(l, i);
		if (!fn->is_c)
			vset_dyay(argv, fn->argv);
	}
}

int ymd_call(struct ymd_context *l, struct func *fn, int argc, int method) {
	int rv;
	struct call_info scope;
	int main_fn = (l->info == NULL);
	if (method) ++argc; // Extra arg0: self

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
	vm_copy_args(l, fn, argc);
	// Pop all args
	ymd_pop(l, argc + (method ? 0 : 1));
	if (fn->is_c) {
		rv = (*fn->u.nafn)(l);
		goto ret;
	}
	rv = vm_run(l, fn, argc);
ret:
	if (fn->argv)
		dyay_final(l->vm, fn->argv);
	l->info = l->info->chain;
	return rv;
}

int ymd_ncall(struct ymd_context *l, struct func *fn, int nret, int narg) {
	int rv = ymd_call(l, fn, narg, 0);
	ymd_adjust(l, nret, rv);
	return rv;
}

int ymd_main(struct ymd_mach *vm, struct func *fn, int argc, char *argv[]) {
	int i;
	struct ymd_context *l = ioslate(vm);
	vset_func(ymd_push(l), fn);
	for (i = 0; i < argc; ++i)
		ymd_kstr(l, argv[i], -1);
	if (setjmp(l->jpt)) {
		// TODO:
		return -1;
	}
	return ymd_call(l, fn, argc, 0);
}

