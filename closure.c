#include "value.h"
#include "state.h"
#include "assembly.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


//-----------------------------------------------------------------------------
// Closure functions:
//-----------------------------------------------------------------------------

#define INST_ALIGN 128
#define KSTR_ALIGN 64
#define LVAR_ALIGN 32
#define BIND_ALIGN 32

static int kz_find(struct kstr **kz, int count, const char *z, int n) {
	int i, lzn = (n >= 0) ? n : (int)strlen(z);
	for (i = 0; i < count; ++i) {
		if (kz[i]->len == lzn &&
			memcmp(kz[i]->land, z, lzn) == 0)
			break;
	}
	return i;
}

struct func *func_new(ymd_nafn_t nafn) {
	struct func *x = gc_alloc(&vm()->gc, sizeof(*x), T_FUNC);
	if (nafn)
		x->nafn = nafn;
	return x;
}

void func_final(struct func *fn) {
	if (fn->bind)
		vm_free(fn->bind);
	if (fn->inst)
		vm_free(fn->inst);
	if (fn->kz)
		vm_free(fn->kz);
	if (fn->lz)
		vm_free(fn->lz);
}

int func_emit(struct func *fn, ymd_inst_t inst) {
	assert(asm_op(inst) % 5 == 0);
	assert(fn->nafn == NULL);
	fn->inst = mm_need(fn->inst, fn->n_inst, INST_ALIGN, sizeof(inst));
	fn->inst[fn->n_inst++] = inst;
	return fn->n_inst;
}

int func_kz(struct func *fn, const char *z, int n) {
	int i = kz_find(fn->kz, fn->n_kz, z, n);
	if (i >= fn->n_kz) {
		fn->kz = mm_need(fn->kz, fn->n_kz, KSTR_ALIGN, sizeof(*fn->kz));
		fn->kz[fn->n_kz++] = ymd_kstr(z, n);
		return fn->n_kz - 1;
	}
	return i;
}

int func_lz(struct func *fn, const char *z, int n) {
	int i = kz_find(fn->lz, fn->n_lz, z, n);
	if (i >= fn->n_lz) {
		fn->lz = mm_need(fn->lz, fn->n_lz, LVAR_ALIGN, sizeof(*fn->lz));
		fn->lz[fn->n_lz++] = ymd_kstr(z, n);
		return fn->n_lz - 1;
	}
	return i;
}

int func_bind(struct func *fn, const struct variable *var) {
	fn->bind = mm_need(fn->bind, fn->n_bind, BIND_ALIGN,
	                      sizeof(*fn->bind));
	fn->bind[fn->n_bind++] = *var;
	return fn->n_bind;
}

void func_shrink(struct func *fn) {
	if (fn->inst)
		fn->inst = mm_shrink(fn->inst, fn->n_inst, INST_ALIGN,
		                     sizeof(fn->inst));
	if (fn->kz)
		fn->kz = mm_shrink(fn->kz, fn->n_kz, KSTR_ALIGN,
		                   sizeof(*fn->kz));
	if (fn->lz)
		fn->lz = mm_shrink(fn->lz, fn->n_lz, LVAR_ALIGN,
		                   sizeof(*fn->lz));
	if (fn->bind)
		fn->bind = mm_shrink(fn->bind, fn->n_bind, BIND_ALIGN,
		                     sizeof(*fn->bind));
}
