#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>

//------------------------------------------------------------------
// Skip List:
// -----------------------------------------------------------------
#define MAX_LEVEL 16

static unsigned int rand_range(unsigned int mod) {
	unsigned int raw = (unsigned)rand();
	unsigned int rv = 0;
	rv |= ((raw & 0xfffff000) >> 12) ^ ((unsigned)time(NULL));
	rv |= (raw & 0x00000fff) << 20;
	return rv % mod;
}

static unsigned short randlv() {
	unsigned short lvl = 1;
	while (rand_range(100) < 50 && lvl < MAX_LEVEL)
		++lvl;
	return lvl;
}

static struct sknd *mknode(struct ymd_mach *vm, unsigned short lv) {
	struct sknd *x = mm_zalloc(vm, 1, sizeof(struct sknd) +
	                           (lv - 1) * sizeof(struct sknd*));
	x->n = lv;
	return x;
}

static inline int skls_count(const struct skls *o) {
	int rv = 0;
	const struct sknd *i = o->head;
	while ((i = i->fwd[0]) != NULL)
		++rv;
	return rv;
}

ymd_int_t skls_key_compare(struct ymd_mach *vm, const struct skls *o,
		const struct variable *lhs, const struct variable *rhs) {
	ymd_int_t rv;
	struct ymd_context *l = ioslate(vm);
	if (o->cmp == SKLS_ASC)
		return compare(lhs, rhs);
	if (o->cmp == SKLS_DASC)
		return compare(rhs, lhs);
	setv_func(ymd_push(l), o->cmp);
	*ymd_push(l) = *lhs;
	*ymd_push(l) = *rhs;
	rv = ymd_call(l, o->cmp, 2, 0);
	if (!rv)
		ymd_panic(l, "skls_key_compare() Bad comparing function");
	rv = int4of(l, ymd_top(l, 0));
	ymd_pop(l, 1);
	return rv;
}

static struct sknd *skls_pos(struct ymd_mach *vm, struct skls *o,
		const struct variable *k, struct sknd *update[MAX_LEVEL]) {
	struct sknd *x = o->head;
	int i;
	memset(update, 0, MAX_LEVEL * sizeof(struct sknd*));
	for (i = o->lv - 1; i >= 0; --i) {
		while (x->fwd[i] && skls_key_compare(vm, o, &x->fwd[i]->k, k) < 0)
			x = x->fwd[i];
		update[i] = x;
	}
	return x->fwd[0];
}

static struct sknd *append(struct ymd_mach *vm, struct skls *o,
		struct sknd *update[]) {
	struct sknd *x;
	unsigned short i, lvl = randlv();
	if (lvl > o->lv) {
		for (i = o->lv; i < lvl; ++i)
			update[i] = o->head;
		o->lv = lvl;
	}
	x = mknode(vm, lvl); ++o->count;
	for (i = 0; i < lvl; ++i) {
		x->fwd[i] = update[i]->fwd[i];
		update[i]->fwd[i] = x;
	}
	return x;
}

static struct variable *skfind(struct ymd_mach *vm, const struct skls *o,
		const struct variable *k) {
	struct sknd *x = o->head;
	int i;
	// For get/put operation
	if (vm) {
		for (i = o->lv - 1; i >= 0; --i) {
			while (x->fwd[i] && skls_key_compare(vm, o, &x->fwd[i]->k, k) < 0) {
				x = x->fwd[i];
			}
		}
		x = x->fwd[0];
		return (x && skls_key_compare(vm, o, &x->k, k) == 0) ? &x->v : knil;
	}
	// For pure comparing
	for (i = o->lv - 1; i >= 0; --i) {
		while (x->fwd[i] && compare(&x->fwd[i]->k, k) < 0) {
			x = x->fwd[i];
		}
	}
	x = x->fwd[0];
	return (x && equals(&x->k, k)) ? &x->v : knil;
}

struct skls *skls_new(struct ymd_mach *vm, struct func *order) {
	struct skls *x = gc_new(vm, sizeof(struct skls), T_SKLS);
	x->lv = 0;
	x->cmp = order;
	x->head = mknode(vm, MAX_LEVEL);
	return x;
}

void skls_final(struct ymd_mach *vm, struct skls *o) {
	struct sknd *i = o->head, *p = i;
	assert(i != NULL);
	while (i) {
		p = i;
		i = i->fwd[0];
		mm_free(vm, p, 1, sizeof(struct sknd) +
		        (p->n - 1) * sizeof(struct sknd *));
	}
}

int skls_equals(const struct skls *o, const struct skls *rhs) {
	const struct sknd *i = o->head;
	int count = 0, rhs_count = skls_count(rhs);
	if (o == rhs)
		return 1;
	assert(i != NULL);
	while ((i = i->fwd[0]) != NULL) {
		if (!equals(&i->v, skfind(NULL, rhs, &i->k)))
			return 0;
		++count;
	}
	return (count == rhs_count);
}

int skls_compare(const struct skls *o, const struct skls *rhs) {
	const struct sknd *i = o->head;
	int rv = 0;
	if (o == rhs)
		return 0;
	assert(i != NULL);
	while ((i = i->fwd[0]) != NULL)
		rv += compare(&i->v, skfind(NULL, rhs, &i->k));
	return rv;
}

struct variable *skls_put(struct ymd_mach *vm, struct skls *o,
		const struct variable *k) {
	struct sknd *update[MAX_LEVEL], *x;
	assert(!is_nil(k));
	x = skls_pos(vm, o, k, update);
	if (!x || skls_key_compare(vm, o, &x->k, k) != 0) // Has found k ?
		x = append(vm, o, update);
	x->k = *k;
	return &x->v;
}

struct variable *skls_get(struct ymd_mach *vm, struct skls *o,
		const struct variable *k) {
	assert(!is_nil(k));
	return skfind(vm, o, k);
}

int skls_remove(struct ymd_mach *vm, struct skls *o,
		const struct variable *k) {
	struct sknd *update[MAX_LEVEL], *x = skls_pos(vm, o, k, update);
	int i;
	if (skls_key_compare(vm, o, &x->k, k) != 0)
		return 0;
	for (i = 0; i < o->lv; ++i) {
		if (update[i]->fwd[i] != x) break;
		update[i]->fwd[i] = x->fwd[i];
	}
	mm_free(vm, x, 1, sizeof(struct sknd) +
			(x->n - 1) * sizeof(struct sknd *));
	while (o->lv > 0 && o->head->fwd[o->lv - 1] == NULL)
		--o->lv;
	return 1;
}

// >=
struct sknd *skls_direct(struct ymd_mach *vm, const struct skls *o,
		const struct variable *k) {
	struct sknd *x = o->head;
	int i;
	for (i = o->lv - 1; i >= 0; --i) {
		while (x->fwd[i] && skls_key_compare(vm, o, &x->fwd[i]->k, k) < 0) {
			x = x->fwd[i];
		}
	}
	x = x->fwd[0];
	return x;
}

