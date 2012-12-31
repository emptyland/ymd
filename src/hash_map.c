#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

//------------------------------------------------------------------
// Hash Map:
// -----------------------------------------------------------------
#define KVI_FREE 0
#define KVI_SLOT 1
#define KVI_NODE 2

static size_t hash(const struct variable *v);
static struct kvi *hindex(struct ymd_mach *vm, struct hmap *o,
                          const struct variable *k);
static void resize(struct ymd_mach *vm, struct hmap *o, int shift);

static int log2x(int n) {
	int i;
	for (i = 0; i < (int)sizeof(n)*8; ++i)
		if ((1 << i) > n)
			return i;
	assert(!"No reached.");
	return -1;
}

static inline struct kvi *position(const struct hmap *o,
                                   size_t h) {
	return o->item + (((h - 1) | 1) % (1 << o->shift));
}


static inline size_t hash_int(ymd_int_t i) {
	return i;
}

static inline size_t hash_float(ymd_float_t f) {
	size_t i;
	memcpy(&i, &f, sizeof(i));
	return i;
}


static inline size_t hash_bool(ymd_int_t i) {
	return !i ? 2 : 3;
}

static size_t hash_ext(void *p) {
	size_t h = 0;
	if (sizeof(void*) < sizeof(size_t))
		memcpy(&h, &p, sizeof(p));
	else
		memcpy(&h, &p, sizeof(h));
	h /= sizeof(void*);
	return h;
}

static size_t hash_dyay(const struct dyay *o) {
	int i = o->count;
	size_t h = i * i;
	while (i--) {
		if (i % 2)
			h ^= hash(o->elem + i);
		else
			h += hash(o->elem + i);
	}
	return h;
}

static size_t hash_hmap(const struct hmap *o) {
	int i = (1 << o->shift);
	size_t h = 0;
	while (i--) {
		if (o->item[i].flag != KVI_FREE) {
			h += hash(&o->item[i].k);
			h ^= hash(&o->item[i].v);
		}
	}
	return h;
}

static size_t hash_skls(const struct skls *o) {
	size_t h = 0;
	struct sknd *x = o->head;
	assert(x != NULL);
	while ((x = x->fwd[0]) != NULL) {
		h += hash(&x->k);
		h ^= hash(&x->v);
	}
	return h;
}

static inline size_t hash_mand(const struct mand *o) {
	size_t h = hash_ext((void *)o->final);
	return h ^ kz_hash((const char *)o->land, o->len);
}

// Lazy hash:
static inline size_t hash_kstr(const struct kstr *kz) {
	struct kstr *mz = (struct kstr *)kz; // FIXME: Maybe unsafe
	return kz->hash ? kz->hash : (mz->hash = kz_hash(kz->land, kz->len));
}

static inline size_t hash_func(const struct func *fn) {
	uintptr_t h = 0, p, i = fn->n_upval;
	while (i--)
		h += hash(fn->upval + i);
	p = (uintptr_t)(fn->is_c ? (void*)fn->u.nafn : fn->u.core);
	p = p / sizeof(void*);
	return (size_t)(p ^ h);
}

static size_t hash(const struct variable *v) {
	switch (ymd_type(v)) {
	case T_NIL:
		return 0;
	case T_INT:
		return hash_int(v->u.i);
	case T_FLOAT:
		return hash_float(v->u.f);
	case T_BOOL:
		return hash_bool(v->u.i);
	case T_KSTR:
		return hash_kstr(kstr_k(v));
	case T_FUNC:
		return hash_func(func_k(v));
	case T_EXT:
		return hash_ext(v->u.ext);
	case T_DYAY:
		return hash_dyay(dyay_k(v));
	case T_HMAP:
		return hash_hmap(hmap_k(v));
	case T_SKLS:
		return hash_skls(skls_k(v));
	case T_MAND:
		return hash_mand(mand_k(v));
	default:
		assert(!"No reached.");
		break;
	}
	return 0;
}

static struct variable *hfind(const struct hmap *o,
                              const struct variable *k) {
	size_t h = hash(k);
	struct kvi *i, *slot = position(o, h);
	switch (slot->flag) {
	case KVI_SLOT:
		for (i = slot; i != NULL; i = i->next) {
			if (i->hash == h && equals(&i->k, k))
				return &i->v;
		}
		break;
	case KVI_FREE:
	case KVI_NODE:
	default:
		break;
	}
	return knil;
}

struct hmap *hmap_new(struct ymd_mach *vm, int count) {
	int shift = 0;
	struct hmap *x = NULL;
	if (count <= 0)
		shift = 5;
	else if (count > 0)
		shift = log2x(count);
	x = gc_new(vm, sizeof(*x), T_HMAP);
	assert(shift > 0);
	x->shift = shift;
	x->item = mm_zalloc(vm, 1 << x->shift, sizeof(struct kvi));
	x->free = x->item + (1 << x->shift) - 1;
	return x;
}

static int hmap_count(const struct hmap *o) {
	int rv = 0, i = (1 << o->shift);
	while (i--) {
		if (o->item[i].flag != KVI_FREE)
			++rv;
	}
	return rv;
}

int hmap_equals(const struct hmap *o, const struct hmap *rhs) {
	int i, rhs_count, count = 0;
	if (o == rhs)
		return 1;
	i = (1 << o->shift);
	if (i != (1 << rhs->shift))
		return 0;
	rhs_count = hmap_count(rhs);
	while (i--) {
		if (o->item[i].flag != KVI_FREE) {
			const struct kvi *it = o->item + i;
			if (!equals(&it->v, hfind(rhs, &it->k)))
				return 0;
			++count;
		}
	}
	return count == rhs_count;
}

int hmap_compare(const struct hmap *o, const struct hmap *rhs) {
	int i, rv = 0;
	if (o == rhs)
		return 0;
	i = (1 << o->shift);
	while (i--) {
		if (o->item[i].flag != KVI_FREE) {
			const struct kvi *it = o->item + i;
			rv += compare(&it->v, hfind(rhs, &it->k));
		}
	}
	return rv;
}

void hmap_final(struct ymd_mach *vm, struct hmap *o) {
	mm_free(vm, o->item, 1 << o->shift, sizeof(*o->item));
}

static struct kvi *alloc_free(struct hmap *o) {
	const struct kvi *first = o->item;
	struct kvi *i;
	for (i = o->free; i > first; --i)
		if (i->flag == KVI_FREE)
			return i;
	return NULL;
}

struct kvi *index_if_head(struct ymd_mach *vm, struct hmap *o,
                          const struct variable *k,
                          struct kvi *slot, size_t h) {
	struct kvi *pos = slot;
	struct kvi *i, *fnd;
	for (i = slot; i != NULL; i = i->next) {
		pos = i;
		if (i->hash == h && equals(&i->k, k))
			return i;
	}
	fnd = alloc_free(o);
	if (!fnd) {
		resize(vm, o, o->shift + 1);
		return hindex(vm, o, k);
	}
	assert(!pos->next);
	fnd->hash = h;
	fnd->flag = KVI_NODE;
	fnd->next = pos->next;
	pos->next = fnd;
	return fnd;
}

static void resize(struct ymd_mach *vm, struct hmap *o, int shift) {
	struct kvi *bak;
	const struct kvi *last, *i;
	int total_count, old_count = 1 << o->shift;
	assert(shift > o->shift);
	// Backup old data
	bak = o->item;
	total_count = o->shift == 0 ? 0 : (1 << o->shift);
	last = bak + total_count;
	// Allocate new slots
	total_count = (1 << shift);
	o->item = mm_zalloc(vm, total_count, sizeof(*o->item));
	o->free = o->item + total_count - 1; // To last node!!
	o->shift = shift;
	// Rehash
	for (i = bak; i < last; ++i) {
		if (i->flag != KVI_FREE) {
			struct variable *pv = hmap_put(vm, o, &i->k);
			assert(pv != NULL);
			//assert(is_nil(pv));
			*pv = i->v;
		}
	}
	mm_free(vm, bak, old_count, sizeof(*o->item));
}

struct kvi *index_if_node(struct ymd_mach *vm, struct hmap *o,
                          const struct variable *k,
                          struct kvi *slot, size_t h) {
	struct kvi *fnd = alloc_free(o), *prev;
	size_t slot_h;
	if (!fnd) {
		resize(vm, o, o->shift + 1);
		return hindex(vm, o, k);
	}
	// Find real head node `prev`.
	slot_h = hash(&slot->k);
	prev = position(o, slot_h);
	while (prev->next != slot) {
		prev = prev->next;
		assert(prev);
	}
	// `prev` is `slot`'s prev node now.
	// Link new node: `fnd`.
	fnd->hash  = slot_h;
	fnd->flag  = KVI_NODE;
	fnd->k     = slot->k;
	fnd->next  = slot->next;
	fnd->v     = slot->v;
	prev->next = fnd;
	// Change `slot` to new head node.
	slot->hash = h;
	slot->flag = KVI_SLOT;
	slot->next = NULL;
	return slot;
}

static struct kvi *hindex(struct ymd_mach *vm, struct hmap *o,
                          const struct variable *k) {
	size_t h = hash(k);
	struct kvi *slot = position(o, h);
	switch (slot->flag) {
	case KVI_FREE:
		slot->flag = KVI_SLOT;
		slot->hash = h;
		return slot;
	case KVI_SLOT:
		return index_if_head(vm, o, k, slot, h);
	case KVI_NODE:
		return index_if_node(vm, o, k, slot, h);
	default:
		assert(!"No reached.");
		break;
	}
	return NULL;
}

struct variable *hmap_put(struct ymd_mach *vm, struct hmap *o,
                          const struct variable *k) {
	struct kvi *x = NULL;
	assert(!is_nil(k));
	x = hindex(vm, o, k);
	x->k = *k;
	return &x->v;
}

struct variable *hmap_get(struct hmap *o, const struct variable *k) {
	assert(!is_nil(k));
	return hfind(o, k);
}

int hmap_remove(struct ymd_mach *vm, struct hmap *o,
                const struct variable *k) {
	size_t h = hash(k);
	struct kvi dummy, *p, *i, *slot = position(o, h);
	(void)vm;
	if (slot->flag != KVI_SLOT)
		return 0;
	memset(&dummy, 0, sizeof(dummy));
	dummy.next = slot;
	p = &dummy;
	for (i = slot; i != NULL; i = i->next) {
		if (i->hash == h && equals(&i->k, k)) {
			p->next = i->next;
			memset(i, 0, sizeof(*i));
			if (dummy.next && dummy.next != slot) {
				memcpy(slot, dummy.next, sizeof(*slot));
				memset(dummy.next, 0, sizeof(*dummy.next));
				slot->flag = KVI_SLOT;
			}
			return 1;
		}
		p = i;
	}
	return 0;
}

