#include "core.h"
#include "assembly.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define INST_ALIGN 128
#define KVAL_ALIGN 64
#define LVAR_ALIGN 32
#define UVAR_ALIGN 8

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

int blk_kval(struct ymd_mach *vm, struct chunk *core, struct hmap *map,
		const struct variable *k) {
	struct variable *val;
	assert (!is_nil(k));
	val = hmap_put(vm, map, k);
	if (is_nil(val)) {
		*blk_klast(vm, core) = *k;
		setv_int(val, core->kkval);
		return core->kkval++;
	}
	assert (ymd_type(val) == T_INT);
	return (int)val->u.i;
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
	if (core->uz)
		mm_free(vm, core->uz, core->kuz, sizeof(*core->uz));
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
	core->klz++;
	return core->klz - 1;
}

int blk_find_uz(struct chunk *core, const char *z) {
	int rv = kz_find(core->uz, core->kuz, z, -1);
	return rv >= core->kuz ? -1 : rv;
}

int blk_add_uz(struct ymd_mach *vm, struct chunk *core, const char *z) {
	int rv = blk_find_uz(core, z);
	if (rv >= 0)
		return -1;
	core->uz = mm_need(vm, core->uz, core->kuz, UVAR_ALIGN,
	                   sizeof(*core->uz));
	core->uz[core->kuz] = kstr_fetch(vm, z, -1);
	core->kuz++;
	return core->kuz - 1;
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
	if (core->uz)
		core->uz = mm_shrink(vm, core->uz, core->kuz, UVAR_ALIGN,
		                     sizeof(*core->uz));
}

//-----------------------------------------------------------------------------
// Closure functions:
//-----------------------------------------------------------------------------
static void func_init(struct ymd_mach *vm, struct func *fn,
                      const char *name) {
	assert(fn->name == NULL);
	fn->name = kstr_fetch(vm, name, -1);
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
	x->n_upval = blk->kuz;
	x->is_c = 0;
	if (name) func_init(vm, x, name);
	return x;
}

void func_final(struct ymd_mach *vm, struct func *fn) {
	if (fn->upval)
		mm_free(vm, fn->upval, fn->n_upval, sizeof(*fn->upval));
	if (fn->is_c)
		return;
	if (fn->u.core->ref > 1) { // drop it!
		--(fn->u.core->ref);
		return;
	}
	blk_final(vm, fn->u.core);
	mm_drop(vm, fn->u.core, sizeof(*fn->u.core));
}

struct kstr *func_proto(struct ymd_mach *vm, struct func *fn) {
	char buf[1024];
	return kstr_fetch(vm, func_proto_z(fn, buf, sizeof(buf)), -1);
}

const char *func_proto_z(const struct func *fn, char *buf, size_t len) {
	char args[1024] = {0};
	int i;
	if (fn->is_c) {
		snprintf(buf, len, "func %s(...) {[native:%p]}",
				fn->name->land, fn->u.nafn);
		return buf;
	}
	for (i = 0; i < fn->u.core->kargs; ++i) {
		if (i) strcat(args, ", ");
		strcat(args, fn->u.core->lz[i]->land);
	}
	snprintf(buf, len, "func %s(%s)", fn->name->land, args);
	return buf;
}

struct variable *func_bind(struct ymd_mach *vm, struct func *fn, int i) {
	assert(i >= 0);
	assert(i < fn->n_upval);
	if (!fn->upval) // Lazy allocating
		fn->upval = mm_zalloc(vm, fn->n_upval, sizeof(*fn->upval));
	return fn->upval + i;
}

void func_dump(struct func *fn, FILE *fp) {
	int i;
	struct chunk *core = fn->u.core;
	assert(!fn->is_c);
	fprintf(fp, "Constant string:\n");
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
	x->name = fn->name;
	x->is_c = fn->is_c;
	x->n_upval = fn->n_upval;
	x->u.core = mm_grab(fn->u.core);
	return x;
}

int func_equals(const struct func *o, const struct func *rhs) {
	size_t i = o->n_upval;
	if (o->is_c != rhs->is_c)
		return 0;
	if (!kstr_equals(o->name, rhs->name))
		return 0;
	if (o->n_upval != rhs->n_upval)
		return 0;
	while (i--) {
		if (!equals(o->upval + i, rhs->upval + i))
			return 0;
	}
	return o->is_c ? o->u.nafn == rhs->u.nafn : o->u.core == rhs->u.core;
}

#define safe_cmp(lhs, rhs) \
	((lhs) < (rhs) ? -1 : ((lhs) > (rhs) ? 1 : 0))
int func_compare(const struct func *o, const struct func *rhs) {
	int i = YMD_MIN(o->n_upval, rhs->n_upval), rv = 0;
	rv += o->is_c - rhs->is_c;
	rv += kstr_compare(o->name, rhs->name);
	while (i--)
		rv += compare(o->upval + i, rhs->upval + i);
	rv += safe_cmp(o->n_upval, rhs->n_upval);
	if (o->is_c == rhs->is_c) {
		if (o->is_c)
			rv += safe_cmp((void*)o->u.nafn, (void*)rhs->u.nafn);
		else
			rv += safe_cmp(o->u.core, rhs->u.core);
	}
	return rv;
}
#undef safe_cmp

