#include "compiler.h"
#include "core.h"
#include "assembly.h"
#include "encoding.h"
#include "third_party/regex/regex.h"
#include <stdio.h>

#define call_init(l, x) { \
	int __k = func_nlocal(l->info->run); \
	memset((x), 0, sizeof(*(x))); \
	(x)->loc = l->info->loc + __k; \
	memset((x)->loc, 0, __k * sizeof(*(x)->loc)); \
} (void)0

#define call_root_init(l, x) { \
	memset((x), 0, sizeof(*(x))); \
	(x)->loc = l->loc; \
} (void)0

#define call_final(l) { \
	l->info = l->info->chain; \
} (void)0

#define call_restore(l, curr) l->info = (curr)->chain

#define call_jenter(l, x, lv) { \
	memset (x, 0, sizeof(struct call_jmpbuf)); \
	(x)->chain = l->jpt; \
	(x)->level = lv; \
	l->jpt = (x); \
} (void)0

#define call_jleave(l) { l->jpt = l->jpt->chain; } (void)0

// +----------------------+
// |  bind  |    local    |
// +--------+-------------+
// | n_bind |n_lz - n_bind|
// +----------------------+
static inline struct variable *vm_local(struct ymd_context *l,
                                        struct func *fn, int i) {
	assert(fn == l->info->run && "Not current running.");
	return l->info->loc + i;
}

static inline struct variable *vm_find_local(struct ymd_context *l,
                                             struct func *fn,
                                             const char *name) {
	int i = blk_find_lz(fn->u.core, name);
	if (i < 0)
		ymd_panic(l, "Can not find local: %s", name);
	return vm_local(l, fn, i);
}

static inline const struct variable *do_keyz(struct func *fn, int i,
                                             struct variable *key) {
	struct chunk *core = fn->u.core;
	assert(i >= 0);
	assert(i < core->kkval);
	assert(TYPEV(core->kval + i) == T_KSTR);
	*key = core->kval[i];
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
		ymd_panic(ioslate(vm), "Regex match fatal: %s", msg);
		return 0;
	}
	err = regexec(&regex, lhs->land, 0, NULL, 0);
	regfree(&regex);
	return !err;
}

static inline int vm_bool(const struct variable *lhs) {
	switch (TYPEV(lhs)) {
	case T_NIL:
		return 0;
	case T_BOOL:
		return lhs->u.i;
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
		ymd_panic(ioslate(vm), "Value can not be `nil` in k-v pair");
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
		assert(!"No reached.");
		break;
	}
}

static inline void vm_iput(struct ymd_mach *vm,
                          struct variable *var,
						  const struct variable *k,
                          const struct variable *v) {
	struct variable *rv = vm_put(vm, var, k);
	if (is_nil(v))
		ymd_panic(ioslate(vm), "Value can not be `nil` in k-v pair");
	*rv = *v;
}

//-----------------------------------------------------------------------------
// Stack functions:
// ----------------------------------------------------------------------------
// Close function's upval, make it to closure:
static int vm_close_upval(struct ymd_context *l, struct func *fn) {
	struct chunk *core = fn->u.core;
	int k, linked = 0;
	assert (!fn->upval && "Can not close a function again.");
	fn->upval = mm_zalloc(l->vm, core->kuz, sizeof(*fn->upval));
	for (k = 0; k < core->kuz; ++k) {
		struct call_info *ci = l->info;
		while (ci) {
			int i;
			if (ci->run->is_c)
				goto goon;
			i = blk_find_lz(ci->run->u.core, core->uz[k]->land);
			if (i >= 0) {
				fn->upval[k] = ci->loc[i];
				++linked;
				break;
			}
			i = blk_find_uz(ci->run->u.core, core->uz[k]->land);
			if (i >= 0) {
				fn->upval[k] = ci->run->upval[i];
				++linked;
				break;
			}
		goon:
			ci = ci->chain;
		}
	}
	if (linked != core->kuz)
		ymd_panic(l, "Some upval has no linked yet.");
	return 0;
}

int vm_calc(struct ymd_context *l, unsigned op) {
	switch (op) {
	case F_INV: {
		struct variable *opd = ymd_top(l, 0);
		opd->u.i = -int_of(l, opd);
		} break;
	case F_MUL: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		rhs->u.i = int_of(l, rhs) * int_of(l, lhs);
		ymd_pop(l, 1);
		} break;
	case F_DIV: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		ymd_int_t opd2 = int_of(l, lhs);
		if (opd2 == 0LL)
			ymd_panic(l, "Div to zero");
		rhs->u.i = int_of(l, rhs) / opd2;
		ymd_pop(l, 1);
		} break;
	case F_ADD: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		switch (TYPEV(rhs)) {
		case T_INT:
			rhs->u.i = rhs->u.i + int_of(l, lhs);
			break;
		case T_KSTR:
			rhs->u.ref = gcx(vm_strcat(l->vm, kstr_of(l, rhs),
			                           kstr_of(l, lhs)));
			gc_release(rhs->u.ref);
			break;
		default:
			ymd_panic(l, "Operator + don't support this type");
			break;
		}
		ymd_pop(l, 1);
		} break;
	case F_SUB: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		rhs->u.i = int_of(l, rhs) - int_of(l, lhs);
		ymd_pop(l, 1);
		} break;
	case F_MOD: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		ymd_int_t opd1 = int_of(l, lhs);
		if (opd1 == 0LL)
			ymd_panic(l, "Mod to zero");
		rhs->u.i = int_of(l, rhs) % int_of(l, lhs);
		ymd_pop(l, 1);
		} break;
	case F_ANDB: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		rhs->u.i = int_of(l, rhs) & int_of(l, lhs);
		ymd_pop(l, 1);
		} break;
	case F_ORB: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		rhs->u.i = int_of(l, rhs) | int_of(l, lhs);
		ymd_pop(l, 1);
		} break;
	case F_XORB: {
		struct variable *rhs = ymd_top(l, 1),
						*lhs = ymd_top(l, 0);
		rhs->u.i = int_of(l, rhs) ^ int_of(l, lhs);
		ymd_pop(l, 1);
		} break;
	case F_INVB: {
		struct variable *opd0 = ymd_top(l, 0);
		opd0->u.i = ~opd0->u.i;
		} break;
	case F_NOT: {
		struct variable *opd = ymd_top(l, 0);
		setv_bool(opd, !vm_bool(opd));
		} break;
	}
	return 0;
}

#define IMPL_JUMP                    \
	switch (asm_flag(inst)) {        \
	case F_FORWARD:                  \
		info->pc += asm_param(inst); \
		break;                       \
	case F_BACKWARD:                 \
		info->pc -= asm_param(inst); \
		break;                       \
	default:                         \
		assert(!"No reached.");      \
		break;                       \
	}
int vm_run(struct ymd_context *l, struct func *fn, int argc) {
	uint_t inst;
	struct call_info *info = l->info;
	struct chunk *core = fn->u.core;
	struct ymd_mach *vm = l->vm;

	(void)argc;
	assert(fn == info->run && "Not current be running.");

retry:
	while (info->pc < core->kinst) {
		inst = core->inst[info->pc];
		l->vm->tick++;
		switch (asm_op(inst)) {
		case I_PANIC:
			ymd_panic(l, "%s Panic!", fn->proto->land);
			break;
		case I_STORE:
			switch (asm_flag(inst)) {
			case F_LOCAL:
				*vm_local(l, fn, asm_param(inst)) = *ymd_top(l, 0);
				break;
			case F_UP:
				fn->upval[asm_param(inst)] = *ymd_top(l, 0);
				break;
			case F_OFF:
				vm_iputg(vm, asm_param(inst), ymd_top(l, 0));
				break;
			}
			ymd_pop(l, 1);
			break;  
		case I_RET:
			return asm_param(inst); // return!
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
				switch (asm_flag(inst)) {
				case F_FORWARD:
					info->pc += asm_param(inst);
					break;
				case F_BACKWARD:
					info->pc -= asm_param(inst);
					break;
				default:
					assert(!"No reached.");
					break;
				}
				ymd_pop(l, 1);
				goto retry;
			}
			} break;
		case I_SETF: {
			switch (asm_flag(inst)) {
			case F_STACK: {
				int i, k = asm_param(inst) << 1;
				struct variable *var = ymd_top(l, k);
				for (i = 0; i < k; i += 2)
					vm_iput(vm, var, ymd_top(l, i + 1), ymd_top(l, i));
				ymd_pop(l, k + 1);
				} break;
			case F_FAST:
				vm_iput(vm, ymd_top(l, 1), &core->kval[asm_param(inst)],
				        ymd_top(l, 0));
				ymd_pop(l, 2);
				break;
			default:
				assert(!"No reached.");
				break;
			}
			} break;
		case I_PUSH: {
			struct variable var;
			switch (asm_flag(inst)) {
			case F_KVAL:
				var = core->kval[asm_param(inst)];
				break;
			case F_LOCAL:
				var = *vm_local(l, fn, asm_param(inst));
				break;
			case F_BOOL:
				setv_bool(&var, asm_param(inst));
				break;
			case F_NIL:
				setv_nil(&var);
				break;
			case F_OFF:
				var = *vm_igetg(vm, asm_param(inst));
				break;
			case F_UP: // Push Upval
				var = fn->upval[asm_param(inst)];
				break;
			}
			*ymd_push(l) = var;
			} break;
		case I_CLOSE: {
			// TODO: like push instruction
			struct variable *opd = &core->kval[asm_param(inst)];
			struct func *copied = func_clone(vm, func_of(l, opd));
			vm_close_upval(l, copied);
			gc_release(copied);
			setv_func(ymd_push(l), copied);
			} break;
		case I_TEST: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			switch (asm_flag(inst)) {
			case F_EQ:
				rhs->u.i = equals(rhs, lhs);
				break;
			case F_NE:
				rhs->u.i = !equals(rhs, lhs);
				break;
			case F_GT:
				rhs->u.i = compare(rhs, lhs) > 0;
				break;
			case F_GE:
				rhs->u.i = compare(rhs, lhs) >= 0;
				break;
			case F_LT:
				rhs->u.i = compare(rhs, lhs) < 0;
				break;
			case F_LE:
				rhs->u.i = compare(rhs, lhs) <= 0;
				break;
			case F_MATCH:
				rhs->u.i = vm_match(vm, kstr_of(l, rhs), kstr_of(l, lhs));
				break;
			default:
				assert(!"No reached.");
				break;
			}
			rhs->tt = T_BOOL;
			ymd_pop(l, 1);
			} break;
		case I_GETF: {
			switch (asm_flag(inst)) {
			case F_STACK: {
				struct variable *k = ymd_top(l, 0),
								*v = vm_get(vm, ymd_top(l, 1), k);
				ymd_pop(l, 2);
				*ymd_push(l) = *v;
				} break;
			case F_FAST:
				*ymd_top(l, 0) = *vm_get(vm, ymd_top(l, 0),
				                         &core->kval[asm_param(inst)]);
				break;
			default:
				assert(!"No reached.");
				break;
			}
			} break;
		case I_TYPEOF: {
			const unsigned tt = TYPEV(ymd_top(l, 0));
			assert(tt < T_MAX);
			setv_kstr(ymd_top(l, 0), typeof_kstr(vm, tt));
			} break;
		case I_CALC:
			vm_calc(l, asm_flag(inst));
			break;
		case I_SHIFT: {
			struct variable *rhs = ymd_top(l, 1),
							*lhs = ymd_top(l, 0);
			if (int_of(l, lhs) < 0)
				ymd_panic(l, "Shift must be great than 0");
			switch (asm_flag(inst)) {
			case F_LEFT:
				rhs->u.i = int_of(l, rhs) << int_of(l, lhs);
				break;
			case F_RIGHT_L:
				rhs->u.i = ((ymd_uint_t)int_of(l, rhs)) >> int_of(l, lhs);
				break;
			case F_RIGHT_A:
				rhs->u.i = int_of(l, rhs) >> int_of(l, lhs);
				break;
			default:
				assert(!"No reached.");
				break;
			}
			ymd_pop(l, 1);
			} break;
		case I_CALL: {
			int adjust = asm_aret(inst);
			int argc = asm_argc(inst);
			struct func *called = func_of(l, ymd_top(l, argc));
			ymd_adjust(l, adjust, ymd_call(l, called, argc, 0));
			} break;
		case I_SELFCALL: {
			struct func *called;
			int adjust = asm_aret(inst);
			int argc = asm_argc(inst);
			int imethod = asm_method(inst);
			called = func_of(l, vm_get(vm, ymd_top(l, argc),
			                            &core->kval[imethod]));
			ymd_adjust(l, adjust, ymd_call(l, called, argc, 1));
			} break;
		case I_NEWMAP: {
			struct hmap *map = hmap_new(vm, asm_param(inst));
			int i, n = asm_param(inst) * 2;
			for (i = 0; i < n; i += 2)
				do_put(vm, gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			setv_hmap(ymd_push(l), map);
			gc_release(map);
			} break;
		case I_NEWSKL: {
			struct skls *map = skls_new(vm);
			int i, n = asm_param(inst) * 2;
			for (i = 0; i < n; i += 2)
				do_put(vm, gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			setv_skls(ymd_push(l), map);
			gc_release(map);
			} break;
		case I_NEWDYA: {
			struct dyay *map = dyay_new(vm, 0);
			int i = asm_param(inst);
			while (i--)
				do_put(vm, gcx(map), NULL, ymd_top(l, i));
			ymd_pop(l, asm_param(inst));
			setv_dyay(ymd_push(l), map);
			gc_release(map);
			} break;
		default:
			assert(!"No reached.");
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
	// Lazy creating
	fn->argv = !fn->argv ? dyay_new(l->vm, argc) : fn->argv;
	gc_release(fn->argv);
	if (!fn->is_c) {
		struct chunk *core = fn->u.core;
		const int k = core->kargs < argc ? core->kargs : argc;
		argv = vm_find_local(l, fn, "argv");
		setv_nil(argv);
		i = k;
		while (i--) // Copy to local variable
			l->info->loc[k - i - 1] = *ymd_top(l, i);
	}
	if (argc > 0) {
		i = argc;
		while (i--) // Copy to array: argv
			*dyay_add(l->vm, fn->argv) = *ymd_top(l, i);
		if (!fn->is_c)
			setv_dyay(argv, fn->argv);
	}
}

static int vm_call(struct ymd_context *l, struct call_info *ci,
	               struct func *fn, int argc, int method) {
	int rv;
	if (method) ++argc; // Extra arg0: self
	// Link the call info
	ci->pc = 0;
	ci->run = fn;
	ci->chain = l->info;
	l->info = ci;
	vm_copy_args(l, fn, argc);
	// Pop all args
	ymd_pop(l, argc + (method ? 0 : 1));
	// Run this function
	if (fn->is_c) {
		rv = (*fn->u.nafn)(l);
		goto ret;
	}
	rv = vm_run(l, fn, argc);
ret:
	if (fn->argv)
		dyay_final(l->vm, fn->argv);
	call_final(l);
	return rv;
}

int ymd_call(struct ymd_context *l, struct func *fn, int argc, int method) {
	struct call_info scope;
	call_init(l, &scope);
	return vm_call(l, &scope, fn, argc, method);
}

int ymd_ncall(struct ymd_context *l, struct func *fn, int nret, int narg) {
	int rv = ymd_call(l, fn, narg, 0);
	ymd_adjust(l, nret, rv);
	return rv;
}

int ymd_pcall(struct ymd_context *l, struct func *fn, int argc) {
	int i;
	struct call_jmpbuf jpt;
	struct call_info scope;
	call_init(l, &scope);
	call_jenter(l, &jpt, l->jpt->level + 1);
	if ((i = setjmp(l->jpt->core)) != 0) {
		call_jleave(l);
		call_restore(l, &scope);
		if (i < jpt.level) longjmp(l->jpt->core, i); // Jump to next
		return -i;
	}
	i = vm_call(l, &scope, fn, argc, 0);
	call_jleave(l);
	return i;
}

int ymd_xcall(struct ymd_context *l, int argc) {
	struct func *fn = func_of(l, ymd_top(l, argc));
	int i;
	struct call_jmpbuf jpt;
	struct call_info scope;
	call_root_init(l, &scope); // External call need root call info.
	call_jenter(l, &jpt, 1);
	// Error and panic handler
	if ((i = setjmp(l->jpt->core)) != 0) {
		call_jleave(l);
		call_restore(l, &scope);
		// Don't clear stack, keep the error info.
		return -i;
	}
	i = vm_call(l, &scope, fn, argc, 0);
	call_jleave(l);
	return i;
}

int ymd_main(struct ymd_context *l, int argc, char *argv[]) {
	int i;
	struct func *fn;
	struct call_jmpbuf jpt;
	struct call_info scope;
	if (l->top == l->stk)
		return 0;
	fn = func_of(l, ymd_top(l, 0));
	call_root_init(l, &scope);
	call_jenter(l, &jpt, 1);
	for (i = 0; i < argc; ++i)
		ymd_kstr(l, argv[i], -1);
	if ((i = setjmp(l->jpt->core)) != 0) {
		if (!l->jpt->panic) puts("VM: Unhandled error!");
		call_jleave(l);
		call_restore(l, &scope);
		ymd_pop(l, (int)(l->top - l->stk)); // Keep stack empty!
		return -i;
	}
	i = vm_call(l, &scope, fn, argc, 0);
	call_jleave(l);
	return i;
}

