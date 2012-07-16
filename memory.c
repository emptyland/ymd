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

void *gc_new(struct ymd_mach *vm, size_t size,
               unsigned char type) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *x = vm_zalloc(vm, size);
	track(gc, x);
	gc->used += size;
	assert(type < (1 << 4));
	x->type = type;
	return x;
}

int gc_init(struct ymd_mach *vm, int k) {
	struct gc_struct *gc = &vm->gc;
	gc->alloced = NULL;
	gc->weak = NULL;
	gc->n_alloced = 0;
	gc->threshold = k;
	return 0;
}

void gc_final(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *i = gc->alloced, *p = i;
	while (i) {
		switch (i->type) {
		case T_FUNC:
			func_final(vm, (struct func *)i);
			break;
		case T_DYAY:
			dyay_final(vm, (struct dyay *)i);
			break;
		case T_HMAP:
			hmap_final(vm, (struct hmap *)i);
			break;
		case T_SKLS:
			skls_final(vm, (struct skls *)i);
			break;
		case T_MAND:
			mand_final(vm, (struct mand *)i);
			break;
		}
		p = i;
		i = i->next;
		vm_free(vm, p);
		--gc->n_alloced;
	}
}

void *mm_need(struct ymd_mach *vm, void *raw, int n, int align,
              size_t chunk) {
	char *rv;
	if (n % align)
		return raw;
	//assert(raw != NULL);
	rv = vm_realloc(vm, raw, chunk * (n + align));
	memset(rv + chunk * n, 0, chunk * align);
	return rv;
}

void *mm_shrink(struct ymd_mach *vm, void *raw, int n, int align,
                size_t chunk) {
	void *bak;
	assert(raw != NULL);
	assert(n > 0);
	if (n % align == 0)
		return raw;
	bak = vm_zalloc(vm, chunk * n);
	memcpy(bak, raw, chunk * n);
	vm_free(vm, raw);
	return bak;
}

void mm_drop(struct ymd_mach *vm, void *p) {
	int *ref = p;
	--(*ref);
	if (*ref == 0) vm_free(vm, p);
}
