#include "memory.h"
#include "value.h"
#include "state.h"
#include <assert.h>
#include <stdio.h>

static int track(struct gc_struct *gc, struct gc_node *x) {
	if (gc->n_alloced >= (1 << gc->shift)) {
		size_t cap = (1 << (gc->shift + 1));
		struct gc_node **bak = vm_zalloc(
			sizeof(*bak) * cap);
		memcpy(bak, gc->alloced, sizeof(*bak) * gc->n_alloced);
		vm_free(gc->alloced);
		gc->alloced = bak;
		gc->shift <<= 1;
	}
	gc->alloced[gc->n_alloced++] = x;
	return gc->n_alloced;
}

void *gc_alloc(struct gc_struct *gc, size_t size,
               unsigned char type) {
	struct gc_node *x = vm_zalloc(size);
	track(gc, x);
	assert(type < (1 << 4));
	x->type = type;
	return x;
}

int gc_init(struct gc_struct *gc, int k) {
	gc->shift = 10;
	gc->alloced = vm_zalloc(sizeof(struct gc_node*) * (1 << gc->shift));
	gc->n_alloced = 0;
	gc->k_alloced = k;
	return 0;
}

void gc_final(struct gc_struct *gc) {
	int k = gc->n_alloced;
	//printf("==GC final:\n>gc end: %d\n", gc->n_alloced);
	while (k--) {
		struct gc_node *i = gc->alloced[k];
		switch (i->type) {
		case T_FUNC:
			func_final((struct func *)i);
			break;
		case T_DYAY:
			// TODO:
			break;
		case T_HMAP:
			hmap_final((struct hmap *)i);
			break;
		case T_SKLS:
			skls_final((struct skls *)i);
			break;
		}
		vm_free(i);
		--gc->n_alloced;
	}
	vm_free(gc->alloced);
	//printf(">gc final: %d\n", gc->n_alloced);
}

void *mm_need(void *raw, int n, int align, size_t chunk) {
	char *rv;
	if (n % align)
		return raw;
	assert(raw != NULL);
	rv = vm_realloc(raw, chunk * (n + align));
	memset(rv + chunk * n, 0, chunk * align);
	return rv;
}

void *mm_shrink(void *raw, int n, int align, size_t chunk) {
	void *bak;
	assert(raw != NULL);
	assert(n > 0);
	if (n % align == 0)
		return raw;
	bak = vm_zalloc(chunk * n);
	memcpy(bak, raw, chunk * n);
	vm_free(raw);
	return bak;
}

