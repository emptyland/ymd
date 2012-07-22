#include "state.h"
#include "value.h"
#include "memory.h"

//------------------------------------------------------------------------------
// String
//------------------------------------------------------------------------------
static struct kstr *kstr_new(struct ymd_mach *vm, int raw, const char *z,
                             int count);

static void kpool_copy2(struct gc_node **slot, struct gc_node *x, int shift) {
	int k = 1 << shift;
	struct kstr *kz = (struct kstr *)x;
	struct gc_node **list = slot + kstr_hash(kz) % k;
	x->next = *list;
	*list = x;
}

static void kpool_resize(struct ymd_mach *vm, int shift) {
	struct kpool *kt = &vm->kpool;
	struct gc_node **bak = kt->slot;
	struct gc_node **i, **k = bak + (1 << kt->shift);
	kt->slot = vm_zalloc(vm, (1 << shift) * sizeof(struct gc_node *));
	for (i = bak; i != k; ++i) {
		if (*i) {
			struct gc_node *x = *i;
			while (x) {
				struct gc_node *next = x->next;
				kpool_copy2(kt->slot, x, shift);
				x = next;
			}
		}
	}
	if (bak) vm_free(vm, bak);
	kt->shift = shift;
}

static struct kstr *kpool_insert(struct ymd_mach *vm, const char *z,
                                 int count) {
	struct kpool *kt = &vm->kpool;
	struct kstr *kz = kstr_new(vm, 1, z, count);
	struct gc_node **list;
	if (kt->used >= (1 << kt->shift))
		kpool_resize(vm, kt->shift + 1);
	list = kt->slot + kstr_hash(kz) % (1 << kt->shift);
	kz->next = *list;
	*list = gcx(kz);
	kt->used++;
	return kz;
}

static struct kstr *kpool_index(struct ymd_mach *vm, const char *z,
                                int count) {
	struct kpool *kt = &vm->kpool;
	struct gc_node *i, *list;
	assert (kt->slot);
	list = kt->slot[(kz_hash(z, count) % (1 << kt->shift))];
	for (i = list; i != NULL; i = i->next) {
		struct kstr *x = (struct kstr *)i;
		if (x->len == count && memcmp(x->land, z, count) == 0)
			return x;
	}
	return kpool_insert(vm, z, count);
}

void kpool_init(struct ymd_mach *vm) {
	struct kpool *kt = &vm->kpool;
	kt->used = 0;
	kt->shift = 8;
	kt->slot = vm_zalloc(vm, (1 << kt->shift) * sizeof(struct gc_node *));
}

void kpool_final(struct ymd_mach *vm) {
	struct kpool *kt = &vm->kpool;
	struct gc_node **i, **k = kt->slot + (1 << kt->shift);
	kt->shift = 0;
	for (i = kt->slot; i != k; ++i) {
		if (*i) {
			struct gc_node *x = *i, *p = x;
			while (x) {
				p = x;
				x = x->next;
				gc_del(vm, p);
				kt->used--;
			}
		}
	}
	assert(kt->used == 0);
}

static struct kstr *kstr_new(struct ymd_mach *vm, int raw, const char *z,
                             int count) {
	struct kstr *x = NULL;
	if (count < 0)
		count = strlen(z);
	if (raw) {
		x = mm_zalloc(vm, 1, sizeof(*x) + count);
		x->type = T_KSTR;
		x->marked = GC_GRAY_BIT0;
	} else
		x = gc_new(vm, sizeof(*x) + count, T_KSTR);
	x->len = count;
	if (!z)
		memset(x->land, 0, count + 1);
	else
		memcpy(x->land, z, count);
	return x;
}

struct kstr *kstr_fetch(struct ymd_mach *vm, const char *z, int count) {
	if (count < 0)
		count = strlen(z);
	if (count < MAX_KPOOL_LEN)
		return kpool_index(vm, z, count);
	return kstr_new(vm, 0, z, count);
}

int kstr_equals(const struct kstr *kz, const struct kstr *rhs) {
	if (kz == rhs)
		return 1;
	if (kz->len == rhs->len)
		return memcmp(kz->land, rhs->land, kz->len) == 0;
	return 0;
}

int kstr_compare(const struct kstr *kz, const struct kstr *rhs) {
	return kz_compare((const unsigned char *)kz->land,
	                  kz->len,
					  (const unsigned char *)rhs->land,
					  rhs->len);
}
