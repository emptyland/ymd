#include "value.h"
#include "state.h"
#include "memory.h"
#include <stdlib.h>
#include <assert.h>

#define MAX_ADD 16

struct dyay *dyay_new(struct ymd_mach *vm, int count) {
	struct dyay *x = NULL;
	if (count <= 0)
		count = 0;
	x = gc_new(vm, sizeof(*x), T_DYAY);
	x->count = count;
	x->max = count == 0 ? 0 : count + MAX_ADD;
	if (x->max > 0)
		x->elem = mm_zalloc(vm, x->max, sizeof(*x->elem));
	return x;
}

void dyay_final(struct ymd_mach *vm, struct dyay *arr) {
	if (arr->elem) {
		assert(arr->max > 0);
		mm_free(vm, arr->elem, arr->max, sizeof(*arr->elem));
		arr->elem = NULL;
		arr->count = 0;
		arr->max = 0;
	}
}

int dyay_equals(const struct dyay *arr, const struct dyay *rhs) {
	if (arr == rhs)
		return 1;
	if (arr->count == rhs->count) {
		int i = arr->count;
		while (i--) {
			if (!equals(arr->elem + i, rhs->elem + i))
				return 0;
		}
		return 1;
	}
	return 0;
}

int dyay_compare(const struct dyay *arr, const struct dyay *rhs) {
	int i, k;
	if (arr == rhs)
		return 0;
	k = arr->count < rhs->count ? arr->count : rhs->count;
	for (i = 0; i < k; ++i) {
		if (compare(arr->elem + i, rhs->elem + i) < 0)
			return -1;
		else if (compare(arr->elem + i, rhs->elem + i) > 0)
			return 1;
	}
	if (arr->count < rhs->count)
		return -1;
	else if (arr->count > rhs->count)
		return 1;
	return 0;
}

struct variable *dyay_get(struct dyay *arr, ymd_int_t i) {
	size_t pos = (size_t)i;
	assert(i >= 0LL && i < (ymd_int_t)arr->count);
	return arr->elem + pos;
}

static inline void resize(struct ymd_mach *vm, struct dyay *arr) {
	int old = arr->max;
	arr->max = arr->count * 3 / 2 + MAX_ADD;
	arr->elem = mm_realloc(vm, arr->elem, old, arr->max,
	                       sizeof(*arr->elem));
}

struct variable *dyay_add(struct ymd_mach *vm, struct dyay *arr) {
	if (arr->count >= arr->max) // Resize
		resize(vm, arr);
	memset(arr->elem + arr->count, 0, sizeof(*arr->elem));
	return arr->elem + arr->count++;
}

struct variable *dyay_insert(struct ymd_mach *vm, struct dyay *arr, ymd_int_t i) {
	int j;
	if (arr->count >= arr->max) // Resize
		resize(vm, arr);
	assert(i >= 0);
	assert(i < arr->count);
	j = arr->count;
	while (j-- > i)
		arr->elem[j + 1] = arr->elem[j];
	++arr->count;
	return arr->elem + i;
}
