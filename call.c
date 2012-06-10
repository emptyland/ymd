#include "value.h"
#include "state.h"
#include "memory.h"
#include "assembly.h"
#include <stdio.h>

extern int do_compile(FILE *fp, struct func *fn);

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
	assert(i >= 0);
	assert(i < fn->n_kz);
	k = fn->kz[i];
	key.type = T_KSTR;
	key.value.ref = (struct gc_node *)k;
	return hmap_get(vm()->global, &key);
}


//-----------------------------------------------------------------------------
// Stack functions:
// ----------------------------------------------------------------------------
int vm_run(struct func *fn, int argc) {
	uint_t inst;
	struct context *l = ioslate();
	struct call_info *info = l->info;

	(void)argc;
	assert(fn == info->run);

	while (info->pc < fn->n_inst) {
		inst = fn->inst[info->pc];
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
			// TODO:
			break;
		case I_JMP:
			// TODO:
			break;
		case I_FOREACH:
			// TODO:
			break;
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
				var.value.ref = (struct gc_node *)fn->kz[param];
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
		case I_TEST:
			// TODO:
			break;
		case I_LOGC:
			// TODO:
			break;
		case I_INDEX:
			ymd_index(l);
			break;
		case I_TYPEOF:
			// TODO:
			break;
		case I_INV:
			// TODO:
			break;
		case I_MUL:
			// TODO:
			break;
		case I_DIV:
			// TODO:
			break;
		case I_ADD:
			// TODO:
			break;
		case I_SUB:
			// TODO:
			break;
		case I_MOD:
			// TODO:
			break;
		case I_POW:
			// TODO:
			break;
		case I_ANDB:
			// TODO:
			break;
		case I_ORB:
			// TODO:
			break;
		case I_XORB:
			// TODO:
			break;
		case I_INVB:
			// TODO:
			break;
		case I_SHIFT:
			// TODO:
			break;
		case I_CALL: {
			struct func *called;
			int argc = param, nrt = 0;
			called = func_of(ymd_top(l, argc));
			nrt = func_call(called, argc);
			ymd_pop(l, 1); // Pop the function
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
			var->value.ref = (struct gc_node *)map;
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
			var->value.ref = (struct gc_node *)map;
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
			var->value.ref = (struct gc_node *)map;
			} break;
		case I_BIND:
			// TODO:
			break;
		case I_LOAD: {
			struct variable *var = ymd_push(l);
			struct func *land = vm()->fn[param];
			var->type = T_FUNC;
			var->value.ref = (struct gc_node *)land;
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
	int i, k = fn->kargs < argc ? fn->kargs : argc;
	struct variable *argv = vm_find_local(fn, "argv");
	struct context *l = ioslate();
	// Set argv nil
	argv->type = T_NIL;
	argv->value.i = 0;
	// Copy args
	i = k;
	while (i--) // Copy to local variable
		*vm_local(fn, k - i - 1) = *ymd_top(l, i);
	// Fill variable: `argv`
	if (argc > 0) {
		struct dyay *arr = dyay_new(argc);
		i = argc;
		while (i--)
			*dyay_get(arr, argc - i - 1) = *ymd_top(l, i);
		argv->type = T_DYAY;
		argv->value.ref = (struct gc_node *)arr;
	}
	// Pop all args
	ymd_pop(l, argc);
}

int func_call(struct func *fn, int argc) {
	int rv;
	struct call_info scope;
	struct context *l = ioslate();

	// Link the call info
	scope.pc = 0;
	if (!l->info)
		scope.loc = l->loc;
	else
		scope.loc = l->info->loc + fn->n_lz;
	scope.run = fn;
	scope.chain = l->info;
	l->info = &scope;
	memset(l->info->loc, 0, fn->n_lz * sizeof(struct variable));
	copy_args(fn, argc);
	if (fn->nafn) {
		rv = (*fn->nafn)(ioslate());
		goto ret;
	}
	rv = vm_run(fn, argc);
ret:
	l->info = l->info->chain;
	return rv;
}

struct func *func_compile(FILE *fp) {
	struct func *fn = func_new(NULL);
	int rv = do_compile(fp, fn);
	return rv < 0 ? NULL : fn;
}
