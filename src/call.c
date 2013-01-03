#include "compiler.h"
#include "core.h"
#include "bytecode.h"
#include "encoding.h"
#include "tostring.h"
#include "zstream.h"
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

static YMD_INLINE struct variable *vm_find_local(struct ymd_context *l,
                                             struct func *fn,
                                             const char *name) {
	int i = blk_find_lz(fn->u.core, name);
	if (i < 0)
		ymd_panic(l, "Can not find local: %s", name);
	return l->info->loc + i;
}

static YMD_INLINE void do_put(struct ymd_mach *vm,
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

static YMD_INLINE void vm_iput(struct ymd_mach *vm, struct variable *var,
		const struct variable *k, const struct variable *v) {
	if (is_nil(k))
		ymd_panic(ioslate(vm), "Key can not be `nil' in k-v pair");
	if (is_nil(v))
		vm_remove(vm, var, k);
	else
		*vm_put(vm, var, k) = *v;
}

static YMD_INLINE const struct variable *do_keyz(struct func *fn, int i,
                                             struct variable *key) {
	struct chunk *core = fn->u.core;
	assert(i >= 0);
	assert(i < core->kkval);
	assert(ymd_type(core->kval + i) == T_KSTR);
	*key = core->kval[i];
	return key;
}

static YMD_INLINE struct variable *vm_igetg(struct ymd_mach *vm, int i) {
	struct variable k;
	struct func *fn = ymd_called(ioslate(vm));
	return hmap_get(vm->global, do_keyz(fn, i, &k));
}

static YMD_INLINE void vm_iputg(struct ymd_mach *vm, int i,
		const struct variable *v) {
	struct variable k;
	struct func *fn = ymd_called(ioslate(vm));
	if (is_nil(v))
		hmap_remove(vm, vm->global, do_keyz(fn, i, &k));
	else
		*hmap_put(vm, vm->global, do_keyz(fn, i, &k)) = *v;
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

// Operand is zero?
static YMD_INLINE int vm_zero(const struct variable *var) {
	if (ymd_type(var) == T_INT)
		return var->u.i == 0LL;
	if (ymd_type(var) == T_FLOAT)
		return var->u.f == 0.0f;
	return 0;
}

#define IMPL_ADD(lhs, rhs)                                    \
	if (floatize(lhs, rhs))                                   \
		setv_float(lhs, float4of(l, lhs) + float4of(l, rhs)); \
	else                                                      \
		lhs->u.i = int4of(l, lhs) + int4of(l, rhs)

#define IMPL_SUB(lhs, rhs)                                    \
	if (floatize(lhs, rhs))                                   \
		setv_float(lhs, float4of(l, lhs) - float4of(l, rhs)); \
	else                                                      \
		lhs->u.i = int4of(l, lhs) - int4of(l, rhs)

static int vm_calc(struct ymd_context *l, unsigned op) {
	switch (op) {
	case F_INV: {
		struct variable *opd = ymd_top(l, 0);
		if (ymd_type(opd) == T_INT)
			setv_int(opd, opd->u.i);
		else
			setv_float(opd, float4of(l, opd));
		} break;
	case F_MUL: {
		struct variable *lhs = ymd_top(l, 1),
						*rhs = ymd_top(l, 0);
		if (floatize(lhs, rhs))
			setv_float(lhs, float4of(l, lhs) * float4of(l, rhs));
		else
			lhs->u.i = int4of(l, lhs) * int4of(l, rhs);
		ymd_pop(l, 1);
		} break;
	case F_DIV: {
		struct variable *lhs = ymd_top(l, 1),
						*rhs = ymd_top(l, 0);
		if (vm_zero(rhs))
			ymd_panic(l, "Can not divide by zero.");
		if (floatize(lhs, rhs))
			setv_float(lhs, float4of(l, lhs) / float4of(l, rhs));
		else
			lhs->u.i = int4of(l, lhs) / int4of(l, rhs);
		ymd_pop(l, 1);
		} break;
	case F_ADD: {
		struct variable *lhs = ymd_top(l, 1),
						*rhs = ymd_top(l, 0);
		IMPL_ADD(lhs, rhs);
		ymd_pop(l, 1);
		} break;
	case F_SUB: {
		struct variable *lhs = ymd_top(l, 1),
						*rhs = ymd_top(l, 0);
		IMPL_SUB(lhs, rhs);
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

#define IMPL_JUMP()                  \
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
	}(void)0

#define IMPL_ADDR(lhs, pop)                                            \
	switch (asm_flag(inst)) {                                          \
	case F_LOCAL:                                                      \
		lhs = l->info->loc + asm_param(inst);                          \
		break;                                                         \
	case F_UP:                                                         \
		lhs = fn->upval + asm_param(inst);                             \
		break;                                                         \
	case F_OFF:                                                        \
		lhs = hmap_put(vm, vm->global, core->kval + asm_param(inst));  \
		break;                                                         \
	case F_INDEX:                                                      \
		lhs = vm_get(vm, ymd_top(l, 2), ymd_top(l, 1));                \
		pop += 2;                                                      \
		break;                                                         \
	case F_FIELD:                                                      \
		lhs = vm_put(vm, ymd_top(l, 1), core->kval + asm_param(inst)); \
		pop += 1;                                                      \
		break;                                                         \
	default:                                                           \
		assert(!"No reached.");                                        \
		break;                                                         \
	}(void)0

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
			ymd_panic(l, "%s Eval I_PANIC instruction",
					func_proto(vm, fn)->land);
			break;
		case I_STORE: {
			int pop = 1;
			switch (asm_flag(inst)) {
			case F_LOCAL:
				l->info->loc[asm_param(inst)] = *ymd_top(l, 0);
				break;
			case F_UP:
				fn->upval[asm_param(inst)] = *ymd_top(l, 0);
				break;
			case F_OFF:
				vm_iputg(vm, asm_param(inst), ymd_top(l, 0));
				break;
			case F_INDEX: {
				int i, k = asm_param(inst) << 1;
				struct variable *var = ymd_top(l, k);
				for (i = 0; i < k; i += 2)
					vm_iput(vm, var, ymd_top(l, i + 1), ymd_top(l, i));
				pop += k;
				} break;
			case F_FIELD:
				vm_iput(vm, ymd_top(l, 1), &core->kval[asm_param(inst)],
				        ymd_top(l, 0));
				pop += 1;
				break;
			default:
				assert(!"No reached.");
				break;
			}
			ymd_pop(l, pop);
			} break;
		case I_PUSH: {
			struct variable var;
			switch (asm_flag(inst)) {
			case F_KVAL:
				var = core->kval[asm_param(inst)];
				break;
			case F_LOCAL:
				var = l->info->loc[asm_param(inst)];
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
			case F_ARGV: // Push Argv
				setv_dyay(&var, l->info->u.argv);
				break;
			case F_INDEX:
				var = *vm_get(vm, ymd_top(l, 1), ymd_top(l, 0));
				ymd_pop(l, 2);
				break;
			case F_FIELD:
				var = *vm_get(vm, ymd_top(l, 0),
						core->kval + asm_param(inst));
				ymd_pop(l, 1);
				break;
			default:
				assert (!"No reached!");
				break;
			}
			*ymd_push(l) = var;
			} break;
		case I_INC: {
			struct variable *lhs, *rhs = ymd_top(l, 0);
			int pop = 1;
			IMPL_ADDR(lhs, pop);
			IMPL_ADD(lhs, rhs);
			ymd_pop(l, pop);
			} break;
		case I_DEC: {
			struct variable *lhs, *rhs = ymd_top(l, 0);
			int pop = 1;
			IMPL_ADDR(lhs, pop);
			IMPL_SUB(lhs, rhs);
			ymd_pop(l, pop);
			} break;
		case I_RET:
			return asm_param(inst); // return!
		case I_JNE:
			if (vm_bool(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
			IMPL_JUMP();
			ymd_pop(l, 1);
			goto retry;
		case I_JNT:
			if (vm_bool(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
			IMPL_JUMP();
			goto retry;
		case I_JNN:
			if (!vm_bool(ymd_top(l, 0))) {
				ymd_pop(l, 1);
				break;
			}
			IMPL_JUMP();
			goto retry;
		case I_JMP:
			IMPL_JUMP();
			goto retry;
		case I_FOREACH: {
			if (!is_nil(ymd_top(l, 0)))
				break;
			IMPL_JUMP();
			ymd_pop(l, 1);
			goto retry;
			} break;
		case I_FORSTEP: {
			// [0]: step
			// [1]: end
			// [2]: tmp
			ymd_int_t step = int4of(l, ymd_top(l, 0)),
					  end  = int4of(l, ymd_top(l, 1)),
					  tmp  = int4of(l, ymd_top(l, 2));
			if (step == 0)
				ymd_panic(l, "Zero step make a death loop.");
			ymd_pop(l, 3);
			if ((step > 0 && tmp < end) || (step < 0 && tmp > end))
				break; // Loop continue
			// Loop finalize
			IMPL_JUMP();
			goto retry;
			} break;
		case I_CLOSE: {
			struct variable *opd = &core->kval[asm_param(inst)];
			struct func *copied = func_clone(vm, func_of(l, opd));
			vm_close_upval(l, copied);
			setv_func(ymd_push(l), copied);
			} break;
		case I_TEST: {
			struct variable *lhs = ymd_top(l, 1),
							*rhs = ymd_top(l, 0);
			switch (asm_flag(inst)) {
			case F_EQ:
				lhs->u.i = vm_equals(lhs, rhs);
				break;
			case F_NE:
				lhs->u.i = !vm_equals(lhs, rhs);
				break;
			case F_GT:
				lhs->u.i = vm_compare(lhs, rhs) > 0;
				break;
			case F_GE:
				lhs->u.i = vm_compare(lhs, rhs) >= 0;
				break;
			case F_LT:
				lhs->u.i = vm_compare(lhs, rhs) < 0;
				break;
			case F_LE:
				lhs->u.i = vm_compare(lhs, rhs) <= 0;
				break;
			//TODO: New regex implement 
			// case F_MATCH:
			//	break;
			default:
				assert(!"No reached.");
				break;
			}
			lhs->tt = T_BOOL;
			ymd_pop(l, 1);
			} break;
		case I_TYPEOF: {
			const unsigned tt = ymd_type(ymd_top(l, 0));
			assert(tt < T_MAX);
			setv_kstr(ymd_top(l, 0), typeof_kstr(vm, tt));
			} break;
		case I_CALC:
			vm_calc(l, asm_flag(inst));
			break;
		case I_STRCAT: {
			struct variable *lhs = ymd_top(l, 1),
							*rhs = ymd_top(l, 0);
			if (ymd_type(lhs) != T_KSTR) {
				struct zostream os = ZOS_INIT;
				tostring(&os, lhs);
				setv_kstr(lhs, kstr_fetch(vm, zos_buf(&os), os.last));
				zos_final(&os);
			}
			if (ymd_type(rhs) != T_KSTR) {
				struct zostream os = ZOS_INIT;
				tostring(&os, rhs);
				setv_kstr(rhs, kstr_fetch(vm, zos_buf(&os), os.last));
				zos_final(&os);
			}
			setv_kstr(lhs, vm_strcat(vm, kstr_k(lhs), kstr_k(rhs)));
			ymd_pop(l, 1);
			gc_step(vm);
			} break;
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
			size_t point = vm->gc.used;
			int adjust = asm_aret(inst);
			int argc = asm_argc(inst);
			struct func *called = func_of(l, ymd_top(l, argc));
			ymd_adjust(l, adjust, ymd_call(l, called, argc, 0));
			if (called->is_c && point < vm->gc.used) // Is memory incrmental ?
				gc_step(vm);
			} break;
		case I_SELFCALL: {
			size_t point = vm->gc.used;
			int adjust = asm_aret(inst);
			int argc = asm_argc(inst);
			int imethod = asm_method(inst);
			struct func *called = func_of(l, vm_get(vm, ymd_top(l, argc),
						&core->kval[imethod]));
			ymd_adjust(l, adjust, ymd_call(l, called, argc, 1));
			if (called->is_c && point < vm->gc.used) // Like I_CALL
				gc_step(vm);
			} break;
		case I_NEWMAP: {
			struct hmap *map = hmap_new(vm, asm_param(inst));
			int i, n = asm_param(inst) * 2;
			for (i = 0; i < n; i += 2)
				do_put(vm, gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			setv_hmap(ymd_push(l), map);
			gc_step(vm);
			} break;
		case I_NEWSKL: {
			struct skls *map;
			int i, n = asm_param(inst) * 2;
			switch (asm_flag(inst)) {
			case F_ASC:
				map = skls_new(vm, SKLS_ASC);
				break;
			case F_DASC:
				map = skls_new(vm, SKLS_DASC);
				break;
			case F_USER:
				map = skls_new(vm, func_of(l, ymd_top(l, n)));
				break;
			default:
				assert (!"No reached.");
				break;
			}
			map->marked = GC_FIXED;
			for (i = 0; i < n; i += 2)
				do_put(vm, gcx(map), ymd_top(l, i + 1), ymd_top(l, i));
			ymd_pop(l, n);
			if (map->cmp != SKLS_ASC && map->cmp != SKLS_DASC)
				ymd_pop(l, 1); // pop the comparor.
			setv_skls(ymd_push(l), map);
			map->marked = l->vm->gc.white;
			gc_step(vm);
			} break;
		case I_NEWDYA: {
			struct dyay *map = dyay_new(vm, 0);
			int i = asm_param(inst);
			while (i--)
				do_put(vm, gcx(map), NULL, ymd_top(l, i));
			ymd_pop(l, asm_param(inst));
			setv_dyay(ymd_push(l), map);
			gc_step(vm);
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

static void vm_copy_args(struct ymd_context *l, struct func *fn, int argc,
		int adjust) {
	int i;
	if (!fn->is_c) {
		struct chunk *core = fn->u.core;
		const int k = core->kargs < argc ? core->kargs : argc;
		i = k;
		while (i--) // Copy to local variable
			l->info->loc[k - i - 1] = *ymd_top(l, i);
	}
	if (func_argv(fn))
		l->info->u.argv = dyay_new(l->vm, argc);
	if (argc > 0) {
		// Make argv object, type == dyay
		if (func_argv(fn)) {
			i = argc;
			while (i--) // Copy to array: argv
				*dyay_add(l->vm, l->info->u.argv) = *ymd_top(l, i);
		} else if (fn->is_c) {
			// Record the number of argument for C function.
			// print (1, 2, 3)
			// print [3]
			// 1 [2]  <- lea
			// 2 [1]
			// 3 [0]
			l->info->u.frame = ymd_offset(l, argc - 1);
		}
	}
	l->info->argc = argc;
	l->info->adjust = adjust;
}

static void vm_balance(struct ymd_context *l, int n, int rv) {
	if (rv) {
		struct variable ret = *ymd_top(l, 0);
		ymd_pop(l, n + 1);
		*ymd_push(l) = ret;
	} else {
		ymd_pop(l, n);
	}
	l->info->u.frame = 0;
}

static int vm_call(struct ymd_context *l, struct call_info *ci,
	               struct func *fn, int argc, int method) {
	int rv, balance = 0;
	if (method) ++argc; // Extra arg0: self
	balance = argc + (method ? 0 : 1);
	// Link the call info
	ci->pc = 0;
	ci->run = fn;
	ci->chain = l->info;
	l->info = ci;
	vm_copy_args(l, fn, argc, method);
	// Run this function
	if (fn->is_c) {
		// Pop args after calling.
		rv = (*fn->u.nafn)(l);
		vm_balance(l, balance, rv);
	} else {
		// Pop all args
		ymd_pop(l, balance);
		rv = vm_run(l, fn, argc);
	}
	if (func_argv(fn))
		dyay_final(l->vm, l->info->u.argv);
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
	// Clear fatal flag.
	l->vm->fatal = 0;
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
	// Clear fatal flag.
	l->vm->fatal = 0;
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
	// Clear fatal flag.
	l->vm->fatal = 0;
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

