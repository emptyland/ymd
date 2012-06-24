#include "memory.h"
#include "value.h"
#include "state.h"
#include <assert.h>
#include <stdio.h>

static inline int track(struct gc_struct *gc, struct gc_node *x) {
	x->next = gc->alloced;
	gc->alloced = x;
	return ++gc->n_alloced;
}

void *gc_new(struct gc_struct *gc, size_t size,
               unsigned char type) {
	struct gc_node *x = vm_zalloc(size);
	track(gc, x);
	gc->used += size;
	assert(type < (1 << 4));
	x->type = type;
	return x;
}

int gc_init(struct gc_struct *gc, int k) {
	gc->alloced = NULL;
	gc->weak = NULL;
	gc->n_alloced = 0;
	gc->threshold = k;
	return 0;
}

void gc_final(struct gc_struct *gc) {
	struct gc_node *i = gc->alloced, *p = i;
	while (i) {
		switch (i->type) {
		case T_FUNC:
			func_final((struct func *)i);
			break;
		case T_DYAY:
			dyay_final((struct dyay *)i);
			break;
		case T_HMAP:
			hmap_final((struct hmap *)i);
			break;
		case T_SKLS:
			skls_final((struct skls *)i);
			break;
		case T_MAND:
			mand_final((struct mand *)i);
			break;
		}
		p = i;
		i = i->next;
		vm_free(p);
		--gc->n_alloced;
	}
}

void *gc_zalloc(struct gc_struct *gc, size_t size) {
	assert(size != 0);
	gc->used += size;
	// if (gc->used > gc->threshold) {
	// TODO: will be full gc ...
	// }
	return vm_zalloc(size);
}

void *gc_realloc(struct gc_struct *gc, void *chunk, size_t old,
                 size_t size) {
	assert(size > old);
	gc->used += (size - old);
	return vm_realloc(chunk, size);
}

void gc_release(struct gc_struct *gc, void *chunk, size_t size) {
	assert(gc->used >= size);
	assert(chunk != NULL);
	gc->used -= size;
	vm_free(chunk);
}

void *mm_need(void *raw, int n, int align, size_t chunk) {
	char *rv;
	if (n % align)
		return raw;
	//assert(raw != NULL);
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

void mm_drop(void *p) {
	int *ref = p;
	--(*ref);
	if (*ref == 0) vm_free(p);
}
