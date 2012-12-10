#include "core.h"
#include "assembly.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define INST_ALIGN 128
#define KVAL_ALIGN 64
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
int blk_emit(struct ymd_mach *vm, struct chunk *core, ymd_inst_t inst,
             int line) {
	assert(asm_op(inst) % 5 == 0);
	core->inst = mm_need(vm, core->inst, core->kinst, INST_ALIGN,
	                     sizeof(inst));
	core->line = mm_need(vm, core->line, core->kinst, INST_ALIGN,
	                     sizeof(line));
	core->inst[core->kinst] = inst;
	core->line[core->kinst] = line;
	return ++core->kinst;
}

static struct variable *blk_klast(struct ymd_mach *vm, struct chunk *core) {
	core->kval = mm_need(vm, core->kval, core->kkval, KVAL_ALIGN,
						 sizeof(*core->kval));
	return core->kval + core->kkval;
}

int blk_kz(struct ymd_mach *vm, struct chunk *core, const char *z, int k) {
	int i = core->kkval;
	struct kstr *kz;
	while (i--) {
		if (TYPEV(&core->kval[i]) == T_KSTR &&
			kstr_k(core->kval + i)->len == k &&
			memcmp(kstr_k(core->kval + i)->land, z, k) == 0)
			return i;
	}
	kz = kstr_fetch(vm, z, k);
	setv_kstr(blk_klast(vm, core), kz);
	gc_release(kz);
	return core->kkval++;
}

int blk_ki(struct ymd_mach *vm, struct chunk *core, ymd_int_t n) {
	int i = core->kkval;
	while (i--) {
		if (TYPEV(&core->kval[i]) == T_INT &&
			core->kval[i].u.i == n)
			return i;
	}
	setv_int(blk_klast(vm, core), n);
	return core->kkval++;
}

int blk_kf(struct ymd_mach *vm, struct chunk *core, void *p) {
	int i = core->kkval;
	struct func *fn = p;
	while (i--) {
		if (TYPEV(&core->kval[i]) == T_FUNC &&
		    core->kval[i].u.ref == p)
			return i;
	}
	setv_func(blk_klast(vm, core), fn);
	gc_release(fn);
	return core->kkval++;
}

void blk_final(struct ymd_mach *vm, struct chunk *core) {
	if (core->inst)
		mm_free(vm, core->inst, core->kinst, sizeof(*core->inst));
	if (core->line)
		mm_free(vm, core->line, core->kinst, sizeof(*core->line));
	if (core->kval)
		mm_free(vm, core->kval, core->kkval, sizeof(*core->kval));
	if (core->lz)
		mm_free(vm, core->lz, core->klz, sizeof(*core->lz));
}

// Only find
int blk_find_lz(struct chunk *core, const char *z) {
	int rv = kz_find(core->lz, core->klz, z, -1);
	return rv >= core->klz ? -1 : rv;
}

// Only add
int blk_add_lz(struct ymd_mach *vm, struct chunk *core, const char *z) {
	int rv = blk_find_lz(core, z);
	if (rv >= 0)
		return -1;
	core->lz = mm_need(vm, core->lz, core->klz, LVAR_ALIGN,
	                   sizeof(*core->lz));
	core->lz[core->klz] = kstr_fetch(vm, z, -1);
	gc_release(core->lz[core->klz]);
	core->klz++;
	return core->klz - 1;
}

void blk_shrink(struct ymd_mach *vm, struct chunk *core) {
	if (core->inst)
		core->inst = mm_shrink(vm, core->inst, core->kinst, INST_ALIGN,
		                       sizeof(*core->inst));
	if (core->line)
		core->line = mm_shrink(vm, core->line, core->kinst, INST_ALIGN,
		                       sizeof(*core->line));
	if (core->kval)
		core->kval = mm_shrink(vm, core->kval, core->kkval, KVAL_ALIGN,
		                       sizeof(*core->kval));
	if (core->lz)
		core->lz = mm_shrink(vm, core->lz, core->klz, LVAR_ALIGN,
		                     sizeof(*core->lz));
}

//-----------------------------------------------------------------------------
// Closure functions:
//-----------------------------------------------------------------------------
static void func_init(struct ymd_mach *vm, struct func *fn,
                      const char *name) {
	assert(fn->proto == NULL);
	if (fn->is_c) {
		fn->proto = vm_format(vm, "func %s(...) {[native:%p]}",
							   !name ? "" : name, fn->u.nafn);
	} else {
		fn->proto = vm_format(vm, "func %s(*) {...}",
		                      !name ? "" : name);
	}
	gc_release(fn->proto);
}

struct func *func_new_c(struct ymd_mach *vm, ymd_nafn_t nafn,
                        const char *name) {
	struct func *x = gc_new(vm, sizeof(*x), T_FUNC);
	assert(nafn);
	x->u.nafn = nafn;
	x->is_c = 1;
	func_init(vm, x, name);
	return x;
}

struct func *func_new(struct ymd_mach *vm, struct chunk *blk,
                      const char *name) {
	struct func *x = gc_new(vm, sizeof(*x), T_FUNC);
	assert(blk);
	mm_grab(blk);
	x->u.core = blk;
	x->is_c = 0;
	if (name) func_init(vm, x, name);
	return x;
}

void func_final(struct ymd_mach *vm, struct func *fn) {
	if (fn->bind)
		mm_free(vm, fn->bind, fn->n_bind, sizeof(*fn->bind));
	if (fn->is_c)
		return;
	if (fn->u.core->ref > 1) { // drop it!
		--(fn->u.core->ref);
		return;
	}
	blk_final(vm, fn->u.core);
	mm_drop(vm, fn->u.core, sizeof(*fn->u.core));
}

int func_bind(struct ymd_mach *vm, struct func *fn, int i,
              const struct variable *var) {
	assert(i >= 0);
	assert(i < fn->n_bind);
	if (!fn->bind) // Lazy allocating
		fn->bind = mm_zalloc(vm, fn->n_bind, sizeof(*fn->bind));
	fn->bind[i] = *var;
	return i;
}

void func_dump(struct func *fn, FILE *fp) {
	int i;
	struct chunk *core = fn->u.core;
	assert(!fn->is_c);
	fprintf(fp, "Constant string:\n");
	//for (i = 0; i < core->kkval; ++i)
		// TODO:
	fprintf(fp, "Local variable:\n");
	for (i = 0; i < core->klz; ++i)
		fprintf(fp, "[%d] %s\n", i, core->lz[i]->land);
	fprintf(fp, "Instructions:\n");
	for (i = 0; i < core->kinst; ++i)
		fprintf(fp, "%08x ", core->inst[i]);
	fprintf(fp, "\n");
}

struct func *func_clone(struct ymd_mach *vm, struct func *fn) {
	struct func *x = gc_new(vm, sizeof(*x), T_FUNC);
	assert(!fn->is_c);
	x->proto = fn->proto;
	x->is_c = fn->is_c;
	x->n_bind = fn->n_bind;
	x->u.core = mm_grab(fn->u.core);
	return x;
}

