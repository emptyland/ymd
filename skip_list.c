#include "value.h"
#include "memory.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>

//------------------------------------------------------------------
// Skip List:
// -----------------------------------------------------------------
#define MAX_LEVEL 16

unsigned int rand_range(unsigned int mod) {
	unsigned int raw = (unsigned)rand();
	unsigned int rv = 0;
	rv |= ((raw & 0xfffff000) >> 12) ^ ((unsigned)time(NULL));
	rv |= (raw & 0x00000fff) << 20;
	return rv % mod;
}

unsigned short randlv() {
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

static struct sknd *append(struct ymd_mach *vm, struct skls *list, struct sknd *update[]) {
	struct sknd *x;
	unsigned short i, lvl = randlv();
	if (lvl > list->lv) {
		for (i = list->lv; i < lvl; ++i)
			update[i] = list->head;
		list->lv = lvl;
	}
	x = mknode(vm, lvl); ++list->count;
	for (i = 0; i < lvl; ++i) {
		x->fwd[i] = update[i]->fwd[i];
		update[i]->fwd[i] = x;
	}
	return x;
}

static struct sknd *skindex(struct ymd_mach *vm, struct skls *list,
                            const struct variable *key) {
	struct sknd *x = list->head, *update[MAX_LEVEL];
	int i;
	memset(update, 0, sizeof(update));
	for (i = list->lv - 1; i >= 0; --i) {
		while (x->fwd[i] && compare(&x->fwd[i]->k, key) < 0) {
			x = x->fwd[i];
		}
		update[i] = x;
	}
	x = x->fwd[0];
	return (x && equals(&x->k, key)) ? x : append(vm, list, update);
}

static struct variable *skfind(const struct skls *list,
                               const struct variable *key) {
	struct sknd *x = list->head;
	int i;
	for (i = list->lv - 1; i >= 0; --i) {
		while (x->fwd[i] && compare(&x->fwd[i]->k, key) < 0) {
			x = x->fwd[i];
		}
	}
	x = x->fwd[0];
	return (x && equals(&x->k, key)) ? &x->v : knil; // `knil` means not found;
}

struct skls *skls_new(struct ymd_mach *vm) {
	struct skls *x = gc_new(vm, sizeof(struct skls), T_SKLS);
	x->lv = 0;
	x->head = mknode(vm, MAX_LEVEL);
	return x;
}

void skls_final(struct ymd_mach *vm, struct skls *list) {
	struct sknd *i = list->head, *p = i;
	assert(i != NULL);
	while (i) {
		p = i;
		i = i->fwd[0];
		mm_free(vm, p, 1, sizeof(struct sknd) +
		        (p->n - 1) * sizeof(struct sknd *));
	}
}

static int skls_count(const struct skls *list) {
	int rv = 0;
	const struct sknd *i = list->head;
	while ((i = i->fwd[0]) != NULL)
		++rv;
	return rv;
}

int skls_equals(const struct skls *list, const struct skls *rhs) {
	const struct sknd *i = list->head;
	int count = 0, rhs_count = skls_count(rhs);
	if (list == rhs)
		return 1;
	assert(i != NULL);
	while ((i = i->fwd[0]) != NULL) {
		if (!equals(&i->v, skfind(rhs, &i->k)))
			return 0;
		++count;
	}
	return (count == rhs_count);
}

int skls_compare(const struct skls *list, const struct skls *rhs) {
	const struct sknd *i = list->head;
	int rv = 0;
	if (list == rhs)
		return 0;
	assert(i != NULL);
	while ((i = i->fwd[0]) != NULL)
		rv += compare(&i->v, skfind(rhs, &i->k));
	return rv;
}

struct variable *skls_put(struct ymd_mach *vm, struct skls *list,
                          const struct variable *key) {
	struct sknd *x = NULL;
	assert(!is_nil(key));
	x = skindex(vm, list, key);
	x->k = *key;
	return &x->v;
}

struct variable *skls_get(struct skls *list, const struct variable *key) {
	assert(!is_nil(key));
	return skfind(list, key);
}

