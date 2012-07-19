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

static int gc_record(struct ymd_mach *vm, size_t inc, int neg) {
	struct gc_struct *gc = &vm->gc;
	if (neg) gc->used -= inc; else gc->used += inc;
	return 0;
}

void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *x = vm_zalloc(vm, size);
	track(gc, x);
	assert(type < (1 << 4));
	x->type = type;
	gc_record(vm, size, 0);
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

static void gc_del(struct ymd_mach *vm, void *p) {
	struct gc_node *o = p;
	size_t chunk = 0;
	switch (o->type) {
	case T_KSTR:
		chunk = sizeof(struct kstr);
		chunk += kstr_f(o)->len;
		break;
	case T_FUNC:
		func_final(vm, func_f(o));
		chunk = sizeof(struct func);
		break;
	case T_DYAY:
		dyay_final(vm, dyay_f(o));
		chunk = sizeof(struct dyay);
		break;
	case T_HMAP:
		hmap_final(vm, hmap_f(o));
		chunk = sizeof(struct hmap);
		break;
	case T_SKLS:
		skls_final(vm, skls_f(o));
		chunk = sizeof(struct skls);
		break;
	case T_MAND:
		mand_final(vm, mand_f(o));
		chunk = sizeof(struct mand);
		chunk += mand_f(o)->len;
		break;
	default:
		assert(0);
		return;
	}
	mm_free(vm, o, 1, chunk);
}

void gc_final(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *i = gc->alloced, *p = i;
	while (i) {
		p = i;
		i = i->next;
		gc_del(vm, p);
		--gc->n_alloced;
	}
}

void *mm_zalloc(struct ymd_mach *vm, int n, size_t chunk) {
	assert(n > 0);
	assert(chunk > 0);
	gc_record(vm, n * chunk, 0);
	return vm_zalloc(vm, n * chunk);
}

void *mm_realloc(struct ymd_mach *vm, void *raw, int old, int n,
                 size_t chunk) {
	assert(n > 0);
	assert(chunk > 0);
	assert(n > old);
	gc_record(vm, (n - old) * chunk, 0);
	return vm_realloc(vm, raw, n * chunk);
}

void mm_free(struct ymd_mach *vm, void *raw, int n, size_t chunk) {
	assert(n > 0);
	assert(chunk > 0);
	gc_record(vm, n * chunk, 1);
	vm_free(vm, raw);
}

void *mm_need(struct ymd_mach *vm, void *raw, int n, int align,
              size_t chunk) {
	char *rv;
	if (n % align)
		return raw;
	rv = vm_realloc(vm, raw, chunk * (n + align));
	memset(rv + chunk * n, 0, chunk * align);
	gc_record(vm, chunk * align, 0);
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
	gc_record(vm, chunk * (align - (n % align)), 1);
	return bak;
}

void mm_drop(struct ymd_mach *vm, void *p, size_t size) {
	int *ref = p;
	--(*ref);
	if (*ref == 0)
		mm_free(vm, p, 1, size);
}
