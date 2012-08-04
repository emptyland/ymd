#include "core.h"
#include <stdlib.h>
#include <assert.h>

#define MAX_ADD 16

struct dyay *dyay_new(struct ymd_mach *vm, int max) {
	struct dyay *x = NULL;
	if (max <= 0)
		max = 0;
	x = gc_new(vm, sizeof(*x), T_DYAY);
	x->count = 0;
	x->max = max;
	if (x->max > 0)
		x->elem = mm_zalloc(vm, x->max, sizeof(*x->elem));
	return x;
}

void dyay_final(struct ymd_mach *vm, struct dyay *o) {
	if (o->elem) {
		assert(o->max > 0);
		mm_free(vm, o->elem, o->max, sizeof(*o->elem));
		o->elem  = NULL;
		o->count = 0;
		o->max   = 0;
	}
}

int dyay_equals(const struct dyay *o, const struct dyay *rhs) {
	if (o == rhs)
		return 1;
	if (o->count == rhs->count) {
		int i = o->count;
		while (i--) {
			if (!equals(o->elem + i, rhs->elem + i))
				return 0;
		}
		return 1;
	}
	return 0;
}

int dyay_compare(const struct dyay *o, const struct dyay *rhs) {
	int i, k;
	if (o == rhs)
		return 0;
	k = o->count < rhs->count ? o->count : rhs->count;
	for (i = 0; i < k; ++i) {
		if (compare(o->elem + i, rhs->elem + i) < 0)
			return -1;
		else if (compare(o->elem + i, rhs->elem + i) > 0)
			return 1;
	}
	if (o->count < rhs->count)
		return -1;
	else if (o->count > rhs->count)
		return 1;
	return 0;
}

struct variable *dyay_get(struct dyay *o, ymd_int_t i) {
	size_t pos = (size_t)i;
	assert(i >= 0LL && i < (ymd_int_t)o->count);
	return o->elem + pos;
}

static inline void resize(struct ymd_mach *vm, struct dyay *o) {
	int old = o->max;
	o->max = o->count * 3 / 2 + MAX_ADD;
	o->elem = mm_realloc(vm, o->elem, old, o->max,
	                       sizeof(*o->elem));
}

struct variable *dyay_add(struct ymd_mach *vm, struct dyay *o) {
	if (o->count >= o->max) // Resize
		resize(vm, o);
	memset(o->elem + o->count, 0, sizeof(*o->elem));
	return o->elem + o->count++;
}

struct variable *dyay_insert(struct ymd_mach *vm, struct dyay *o, ymd_int_t i) {
	int j;
	if (o->count >= o->max) // Resize
		resize(vm, o);
	assert(i >= 0);
	assert(i < o->count);
	j = o->count;
	while (j-- > i)
		o->elem[j + 1] = o->elem[j];
	++o->count;
	return o->elem + i;
}

int dyay_remove(struct ymd_mach *vm, struct dyay *o, ymd_int_t i) {
	(void)vm;
	assert(i >= 0);
	if (i >= o->count)
		return 0;
	if (i < o->count - 1)
		memmove(o->elem + i, o->elem + i + 1,
		        (o->count - i - 1) * sizeof(*o->elem));
	--o->count;
	return 1;
}
