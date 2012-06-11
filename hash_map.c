#include "value.h"
#include "memory.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

//------------------------------------------------------------------
// Hash Map:
// -----------------------------------------------------------------
#define KVI_FREE 0
#define KVI_SLOT 1
#define KVI_NODE 2

static size_t hash(const struct variable *v);
static struct kvi *hindex(struct hmap *map, const struct variable *key);
static void resize(struct hmap *map, int shift);

static int log2x(int n) {
	int i;
	for (i = 0; i < (int)sizeof(n)*8; ++i)
		if ((1 << i) > n)
			return i;
	assert(0);
	return -1;
}

static inline struct kvi *position(struct hmap *map,
                                   size_t h) {
	return map->item + (((h - 1) | 1) % (1 << map->shift));
}


static inline size_t hash_int(ymd_int_t i) {
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

static size_t hash_dyay(const struct dyay *arr) {
	int i = arr->count;
	size_t h = i * i;
	while (i--) {
		if (i % 2)
			h ^= hash(arr->elem + i);
		else
			h += hash(arr->elem + i);
	}
	return h;
}

static size_t hash_hmap(const struct hmap *map) {
	int i = (1 << map->shift);
	size_t h = 0;
	while (i--) {
		if (map->item[i].flag != KVI_FREE) {
			h += hash(&map->item[i].k);
			h ^= hash(&map->item[i].v);
		}
	}
	return h;
}

static size_t hash_skls(const struct skls *list) {
	size_t h = 0;
	struct sknd *x = list->head;
	assert(x != NULL);
	while ((x = x->fwd[0]) != NULL) {
		h += hash(&x->k);
		h ^= hash(&x->v);
	}
	return h;
}

static size_t hash_mand(const struct mand *pm) {
	size_t h = hash_ext((void *)pm->final);
	return h ^ kz_hash((const char *)pm->land, pm->len);
}

static size_t hash(const struct variable *v) {
	switch (v->type) {
	case T_NIL:
		return 0;
	case T_INT:
		return hash_int(v->value.i);
	case T_BOOL:
		return hash_bool(v->value.i);
	case T_KSTR:
		return kstr_k(v)->hash;
	//case T_FUNC:
	//	return hash_func(func_k(v));
	case T_EXT:
		return hash_ext(v->value.ext);
	case T_DYAY:
		return hash_dyay(dyay_k(v));
	case T_HMAP:
		return hash_hmap(hmap_k(v));
	case T_SKLS:
		return hash_skls(skls_k(v));
	case T_MAND:
		return hash_mand(mand_k(v));
	default:
		assert(0);
		break;
	}
	return 0;
}

static struct variable *hfind(struct hmap *map,
                              const struct variable *key) {
	size_t h = hash(key);
	struct kvi *i, *slot = position(map, h);
	switch (slot->flag) {
	case KVI_SLOT:
		for (i = slot; i != NULL; i = i->next) {
			if (i->hash == h && equals(&i->k, key))
				return &i->v;
		}
		break;
	case KVI_FREE:
	case KVI_NODE:
	default:
		break;
	}
	return knax;
}

struct hmap *hmap_new(int count) {
	int shift = 0;
	struct hmap *x = NULL;
	if (count <= 0)
		shift = 5;
	else if (count > 0)
		shift = log2x(count);
	x = gc_alloc(&vm()->gc, sizeof(*x), T_HMAP);
	assert(shift > 0);
	x->shift = shift;
	x->item = vm_zalloc(sizeof(struct kvi) * (1 << x->shift));
	x->free = x->item + (1 << x->shift) - 1;
	return x;
}

int hmap_equals(const struct hmap *map, const struct hmap *lhs) {
	int i;
	if (map == lhs)
		return 1;
	i = (1 << map->shift);
	while (i--) {
		if (map->item[i].flag != KVI_FREE) {
			const struct kvi *it = map->item + i;
			if (!equals(&it->v, hfind((struct hmap*)lhs, &it->k)))
				return 0;
		}
	}
	return 1;
}

int hmap_compare(const struct hmap *map, const struct hmap *lhs) {
	int i, rv = 0;
	if (map == lhs)
		return 0;
	i = (1 << map->shift);
	while (i--) {
		if (map->item[i].flag != KVI_FREE) {
			const struct kvi *it = map->item + i;
			rv += compare(&it->v, hfind((struct hmap*)lhs, &it->k));
		}
	}
	return rv;
}

void hmap_final(struct hmap *map) {
	vm_free(map->item);
}

static struct kvi *alloc_free(struct hmap *map) {
	const struct kvi *first = map->item;
	struct kvi *i;
	for (i = map->free; i > first; --i)
		if (i->flag == KVI_FREE)
			return i;
	return NULL;
}

struct kvi *get_head(struct hmap *map, const struct variable *key,
                     struct kvi *slot, size_t h) {
	struct kvi *pos = slot;
	struct kvi *i, *fnd;
	for (i = slot; i != NULL; i = i->next) {
		pos = i;
		if (i->hash == h && equals(&i->k, key))
			return i;
	}
	fnd = alloc_free(map);
	if (!fnd) {
		resize(map, map->shift + 1);
		return hindex(map, key);
	}
	assert(!pos->next);
	fnd->hash = h;
	fnd->flag = KVI_NODE;
	fnd->next = pos->next;
	pos->next = fnd;
	return fnd;
}

static void resize(struct hmap *map, int shift) {
	struct kvi *bak;
	const struct kvi *last, *i;
	int total_count;
	assert(shift > map->shift);
	// Backup old data
	bak = map->item;
	total_count = map->shift == 0 ? 0 : (1 << map->shift);
	last = bak + total_count;
	// Allocate new slots
	map->shift = shift;
	total_count = (1 << map->shift);
	map->item = vm_zalloc(sizeof(*map->item) * total_count);
	map->free = map->item + total_count - 1; // To last node!!
	// Rehash
	for (i = bak; i < last; ++i) {
		if (i->flag != KVI_FREE) {
			struct variable *pv = hmap_get(map, &i->k);
			assert(pv != NULL);
			//assert(is_nil(pv));
			*pv = i->v;
		}
	}
	vm_free(bak);
}

struct kvi *get_any(struct hmap *map, const struct variable *key,
                    struct kvi *slot, size_t h) {
	struct kvi *fnd = alloc_free(map), *prev;
	size_t slot_h;
	if (!fnd) {
		resize(map, map->shift + 1);
		return hindex(map, key);
	}
	// Find real head node `prev`.
	slot_h = hash(&slot->k);
	prev = position(map, slot_h);
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

static struct kvi *hindex(struct hmap *map, const struct variable *key) {
	size_t h = hash(key);
	struct kvi *slot = position(map, h);
	switch (slot->flag) {
	case KVI_FREE:
		slot->flag = KVI_SLOT;
		slot->hash = h;
		return slot;
	case KVI_SLOT:
		return get_head(map, key, slot, h);
	case KVI_NODE:
		return get_any(map, key, slot, h);
	default:
		assert(0);
	}
	return NULL;
}

struct variable *hmap_get(struct hmap *map, const struct variable *key) {
	struct kvi *x = hindex(map, key);
	x->k = *key;
	return &x->v;
}

struct kvi *kz_index(struct hmap *map, const char *z, int n) {
	// Make a fake key.
	struct variable fake;
	struct kvi *x;
	struct kstr *kz;
	char chunk[sizeof(struct kstr) + MAX_CHUNK_LEN];
	int lzn = n >= 0 ? n : (int)strlen(z);
	if (lzn > MAX_CHUNK_LEN) {
		kz = vm_zalloc(sizeof(struct kstr) + lzn);
	} else {
		memset(chunk, 0, sizeof(struct kstr) + lzn);
		kz = (struct kstr *)chunk;
	}
	kz->next = NULL;
	kz->color = 0;
	kz->type = T_KSTR;
	kz->len = lzn;
	kz->hash = kz_hash(z, kz->len);
	memcpy(kz->land, z, kz->len);
	fake.type = T_KSTR;
	fake.value.ref = (struct gc_node *)kz;
	// Find position
	x = hindex(map, &fake);
	if (!equals(&x->k, &fake)) {
		x->k.type = T_KSTR;
		x->k.value.ref = (struct gc_node *)kstr_new(z, kz->len);
	}
	if (kz->len > MAX_CHUNK_LEN) vm_free(kz);
	return x;
}

