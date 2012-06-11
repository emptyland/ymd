#include "value.h"
#include "state.h"
#include "memory.h"
#include <stdlib.h>
#include <assert.h>

#define MAX_ADD 16

struct dyay *dyay_new(int count) {
	struct dyay *x = NULL;
	if (count <= 0)
		count = 0;
	x = gc_alloc(&vm()->gc, sizeof(*x), T_DYAY);
	x->count = count;
	x->max = count == 0 ? 0 : count + MAX_ADD;
	if (x->max > 0)
		x->elem = vm_zalloc(sizeof(*x->elem) * x->max);
	return x;
}

void dyay_final(struct dyay *arr) {
	if (arr->elem) {
		assert(arr->max > 0);
		vm_free(arr->elem);
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
	if (i < 0LL || i >= (ymd_int_t)arr->count)
		return knax;
	return arr->elem + pos;
}

static inline void resize(struct dyay *arr) {
	arr->max = arr->count * 3 / 2 + MAX_ADD;
	if (arr->elem)
		arr->elem = vm_realloc(arr->elem, sizeof(*arr->elem) * arr->max);
	else
		arr->elem = vm_zalloc(sizeof(*arr->elem) * arr->max);
}

struct variable *dyay_add(struct dyay *arr) {
	if (arr->count >= arr->max) // Resize
		resize(arr);
	memset(arr->elem + arr->count, 0, sizeof(*arr->elem));
	return arr->elem + arr->count++;
}
