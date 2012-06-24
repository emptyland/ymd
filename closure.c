#include "value.h"
#include "state.h"
#include "assembly.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define INST_ALIGN 128
#define KSTR_ALIGN 64
#define LVAR_ALIGN 32

static int kz_find(struct kstr **kz, int count, const char *z, int n) {
	int i, lzn = (n >= 0) ? n : (int)strlen(z);
	for (i = 0; i < count; ++i) {
		if (kz[i]->len == lzn &&
			memcmp(kz[i]->land, z, lzn) == 0)
			break;
	}
	return i;
}

//-----------------------------------------------------------------------------
// Chunk functions:
//-----------------------------------------------------------------------------
int blk_emit(struct chunk *core, ymd_inst_t inst) {
	assert(asm_op(inst) % 5 == 0);
	core->inst = mm_need(core->inst, core->kinst, INST_ALIGN, sizeof(inst));
	core->inst[core->kinst++] = inst;
	return core->kinst;
}

int blk_kz(struct chunk *core, const char *z, int n) {
	int i = kz_find(core->kz, core->kkz, z, n);
	if (i >= core->kkz) {
		core->kz = mm_need(core->kz, core->kkz, KSTR_ALIGN, sizeof(*core->kz));
		core->kz[core->kkz++] = ymd_kstr(z, n);
		return core->kkz - 1;
	}
	return i;
}

// Only find
int blk_find_lz(struct chunk *core, const char *z) {
	int rv = kz_find(core->lz, core->klz, z, -1);
	return rv >= core->klz ? -1 : rv;
}

// Only add
int blk_add_lz(struct chunk *core, const char *z) {
	int rv = blk_find_lz(core, z);
	if (rv >= 0)
		return -1;
	core->lz = mm_need(core->lz, core->klz, LVAR_ALIGN, sizeof(*core->lz));
	core->lz[core->klz++] = ymd_kstr(z, -1);
	return core->klz - 1;
}

void blk_shrink(struct chunk *core) {
	if (core->inst)
		core->inst = mm_shrink(core->inst, core->kinst, INST_ALIGN,
		                       sizeof(core->inst));
	if (core->kz)
		core->kz = mm_shrink(core->kz, core->kkz, KSTR_ALIGN,
		                     sizeof(*core->kz));
	if (core->lz)
		core->lz = mm_shrink(core->lz, core->klz, LVAR_ALIGN,
		                     sizeof(*core->lz));
}

//-----------------------------------------------------------------------------
// Closure functions:
//-----------------------------------------------------------------------------
static void func_init(struct func *fn, const char *name) {
	assert(fn->proto == NULL);
	if (fn->is_c) {
		fn->proto = ymd_format("func %s(...) {[native:%p]}",
							   !name ? "" : name, fn->u.nafn);
	} else {
		fn->proto = ymd_format("func %s[*%d] (*%d) {...}",
							   !name ? "" : name, fn->n_bind,
							   fn->u.core->kargs);
	}
}

struct func *func_new_c(ymd_nafn_t nafn, const char *name) {
	struct func *x = gc_alloc(&vm()->gc, sizeof(*x), T_FUNC);
	assert(nafn);
	x->u.nafn = nafn;
	x->is_c = 1;
	func_init(x, name);
	return x;
}

struct func *func_new(struct chunk *blk, const char *name) {
	struct func *x = gc_alloc(&vm()->gc, sizeof(*x), T_FUNC);
	assert(blk);
	mm_grab(blk);
	x->u.core = blk;
	x->is_c = 0;
	func_init(x, name);
	return x;
}

void func_final(struct func *fn) {
	if (fn->bind)
		vm_free(fn->bind);
	if (fn->is_c)
		return;
	if (fn->u.core->ref > 1) { // drop it!
		--(fn->u.core->ref);
		return;
	}
	if (fn->u.core->inst)
		vm_free(fn->u.core->inst);
	if (fn->u.core->kz)
		vm_free(fn->u.core->kz);
	if (fn->u.core->lz)
		vm_free(fn->u.core->lz);
	mm_drop(fn->u.core);
}

int func_bind(struct func *fn, int i, const struct variable *var) {
	assert(i >= 0);
	assert(i < fn->n_bind);
	if (!fn->bind) // Lazy allocating
		fn->bind = vm_zalloc(sizeof(*fn->bind) * fn->n_bind);
	fn->bind[i] = *var;
	return i;
}

void func_dump(struct func *fn, FILE *fp) {
	int i;
	struct chunk *core = fn->u.core;
	assert(!fn->is_c);
	fprintf(fp, "Constant string:\n");
	for (i = 0; i < core->kkz; ++i)
		fprintf(fp, "[%d] %s\n", i, core->kz[i]->land);
	fprintf(fp, "Local variable:\n");
	for (i = 0; i < core->klz; ++i)
		fprintf(fp, "[%d] %s\n", i, core->lz[i]->land);
	fprintf(fp, "Instructions:\n");
	for (i = 0; i < core->kinst; ++i)
		fprintf(fp, "%08x ", core->inst[i]);
	fprintf(fp, "\n");
}

struct func *func_clone(struct func *fn) {
	struct func *x = gc_alloc(&vm()->gc, sizeof(*x), T_FUNC);
	assert(!fn->is_c);
	x->proto = fn->proto;
	x->is_c = fn->is_c;
	x->n_bind = fn->n_bind;
	x->u.core = mm_grab(fn->u.core);
	return x;
}
