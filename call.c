#include "value.h"
#include "state.h"
#include "memory.h"
#include "assembly.h"
#include <stdio.h>

extern int do_compile(FILE *fp, struct func *fn);

struct typeof_z {
	int len;
	const char *name;
};
static const struct typeof_z typeof_name[] = {
	{ 3, "nil", },
	{ 3, "int", },
	{ 4, "bool", },
	{ 4, "lite", },
	{ 6, "string", },
	{ 8, "function", },
	{ 5, "array", },
	{ 7, "hashmap", },
	{ 8, "skiplist", },
	{ 7, "managed", },
};

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
	int i = func_find_lz(fn, name);
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
		//case I_CLOSURE:
		//	break;
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
			if (bool_of(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
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
		case I_JMP:
			switch (flag) {
			case F_FORWARD:
				info->pc += param;
				goto retry;
			case F_BACKWARD:
				info->pc -= param;
				goto retry;
			default:
				assert(0);
				break;
			}
			break;
		case I_FOREACH: {
			struct variable *cond = ymd_top(l, 0);
			if (cond->type == T_EXT &&
				cond->value.ext == (void *)-1) {
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
		case I_SETF:
			ymd_setf(l, param);
			break;
		case I_PUSH: {
			struct variable var;
			switch (flag) {
			case F_INT:
				var.type = T_INT;
				var.value.i = param;
				break;
			case F_PARTAL:
				// TODO:
				break;
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
			default:
				assert(0);
				break;
			}
			rhs->type = T_BOOL;
			ymd_pop(l, 1);
			} break;
		case I_LOGC: {
			struct variable *opd0, *opd1 = ymd_top(l, 0);
			switch (flag) {
			case F_NOT:
				opd1->value.i = !bool_of(opd1);
				break;
			case F_AND:
				opd0 = ymd_top(l, 1);
				opd0->value.i = bool_of(opd0) && bool_of(opd1);
				break;
			case F_OR:
				opd0 = ymd_top(l, 1);
				opd0->value.i = bool_of(opd0) || bool_of(opd1);
				break;
			default:
				assert(0);
				break;
			}
			if (flag != F_NOT)
				ymd_pop(l, 1);
			} break;
		case I_INDEX:
			ymd_index(l);
			break;
		case I_TYPEOF: {
			const unsigned tt = ymd_top(l, 0)->type;
			struct kstr *kz;
			assert(tt < KNAX);
			kz = ymd_kstr(typeof_name[tt].name, typeof_name[tt].len);
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
				// TODO:
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
			struct variable *var;
			int i, n = param * 2;
			for (i = 0; i < n; i += 2)
				*hmap_get(map, ymd_top(l, i + 1)) = *ymd_top(l, i);
			ymd_pop(l, n);
			var = ymd_push(l);
			var->type = T_HMAP;
			var->value.ref = gcx(map);
			} break;
		case I_NEWSKL: {
			struct skls *map = skls_new();
			struct variable *var;
			int i, n = param * 2;
			for (i = 0; i < n; i += 2)
				*skls_get(map, ymd_top(l, i + 1)) = *ymd_top(l, i);
			ymd_pop(l, n);
			var = ymd_push(l);
			var->type = T_SKLS;
			var->value.ref = gcx(map);
			} break;
		case I_NEWDYA: {
			struct dyay *map = dyay_new(0);
			struct variable *var;
			int i = param;
			while (i--)
				*dyay_add(map) = *ymd_top(l, i);
			ymd_pop(l, param);
			var = ymd_push(l);
			var->type = T_DYAY;
			var->value.ref = gcx(map);
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

static void copy_args(struct func *fn, int argc) {
	int i;
	struct variable *argv = NULL;
	struct context *l = ioslate();
	if (!fn->is_c) {
		struct chunk *core = fn->u.core;
		const int k = core->kargs < argc ? core->kargs : argc;
		argv = vm_find_local(fn, "argv");
		argv->type = T_NIL;
		argv->value.i = 0LL;
		// Copy args
		i = k;
		while (i--) // Copy to local variable
			*vm_local(fn, k - i - 1) = *ymd_top(l, i);
	}
	if (argc > 0) {
		// Lazy creating
		fn->argv = !fn->argv ? dyay_new(0) : fn->argv;
		i = argc;
		while (i--)
			*dyay_add(fn->argv) = *ymd_top(l, i);
		if (!fn->is_c) {
			argv->type = T_DYAY;
			argv->value.ref = gcx(fn->argv);
		}
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
	struct variable *p = ymd_push(l);
	p->type = T_FUNC;
	p->value.ref = gcx(fn);
	for (i = 0; i < argc; ++i)
		ymd_push_kstr(l, argv[i], -1);
	return func_call(fn, argc);
}

struct func *func_compile(FILE *fp) {
	struct func *fn = func_new(NULL);
	int rv = do_compile(fp, fn);
	func_init(fn);
	return rv < 0 ? NULL : fn;
}
