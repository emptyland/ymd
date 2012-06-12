#include "value.h"
#include "memory.h"
#include "state.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

//-------------------------------------------------------------------------
// Constant variable value:
//-------------------------------------------------------------------------
static struct variable knax_fake_var = {
	.value = { .i = 0xcccccccccccbadeeULL, },
	.type = KNAX,
};
struct variable *knax = &knax_fake_var;

//-------------------------------------------------------------------------
// Type casting define:
//-------------------------------------------------------------------------
ymd_int_t int_of(const struct variable *var) {
	if (var->type != T_INT)
		vm_die("Variable is not `int`");
	return var->value.i;
}

ymd_int_t bool_of(const struct variable *var) {
	if (var->type != T_BOOL)
		vm_die("Variable is not `bool`");
	return var->value.i;
}

#define DEFINE_REFOF(name, tt)                     \
	struct name *name##_of(struct variable *var) { \
		if (var->type != tt)                       \
			vm_die("Variable is not `"#name"`");   \
		assert(var->value.ref != NULL);            \
		return (struct name *)var->value.ref;      \
	}
DECL_TREF(DEFINE_REFOF)
#undef DEFINE_REFOF

//-------------------------------------------------------------------------
// Generic functions:
//-------------------------------------------------------------------------
int equals(const struct variable *lhs, const struct variable *rhs) {
	if (lhs == rhs)
		return 1;
	if (lhs->type != rhs->type)
		return 0;
	switch (lhs->type) {
	case T_NIL:
		return 1;
	case T_INT:
	case T_BOOL:
		return lhs->value.i == rhs->value.i;
	case T_KSTR:
		return kstr_equals(kstr_k(lhs), kstr_k(rhs));
	//case T_CLOSURE:
	//	break;
	case T_EXT:
		return lhs->value.ext == rhs->value.ext;
	case T_DYAY:
		return dyay_equals(dyay_k(lhs), dyay_k(rhs));
	case T_HMAP:
		return hmap_equals(hmap_k(lhs), hmap_k(lhs));
	case T_SKLS:
		return skls_equals(skls_k(rhs), skls_k(lhs));
	//case T_MAND:
	//	return mand_equals(mand_k(lhs), mand_k(rhs));
	default:
		assert(0);
		break;
	}
	return 0;
}

#define safe_compare(l, r) \
	(((l) < (r)) ? -1 : (((l) > (r)) ? 1 : 0))

int compare(const struct variable *lhs, const struct variable *rhs) {
	if (lhs == rhs)
		return 0;
	if (lhs->type != rhs->type)
		return safe_compare(lhs->type, rhs->type);
	switch (lhs->type) {
	case T_NIL:
		return 0; // Do not compare a nil value.
	case T_INT:
	case T_BOOL:
		return safe_compare(lhs->value.i, rhs->value.i);
	case T_KSTR:
		return kstr_compare(kstr_k(lhs), kstr_k(rhs));
	//case T_CLOSURE:
	//	break;
	case T_EXT:
		return 0; // Do not compare a pointer.
	case T_DYAY:
		return dyay_compare(dyay_k(lhs), dyay_k(rhs));
	case T_HMAP:
		return hmap_compare(hmap_k(lhs), hmap_k(rhs));
	case T_SKLS:
		return skls_compare(skls_k(lhs), skls_k(rhs));
	//case T_MAND:
	//	return mand_compare(mand_k(lhs), mand_k(rhs));
	default:
		assert(0);
		break;
	}
	return 0;
}

#undef safe_compare

size_t kz_hash(const char *z, int i) {
	size_t n = 1315423911U;
	while(i--)
		n ^= ((n << 5) + (*z++) + (n >> 2));
	return n;
}

static int kz_compare(const unsigned char *z1, int n1,
                      const unsigned char *z2, int n2) {
	int i, n;
	if (z1 == z2)
		return 0;
	if (n1 <= n2)
		n = n1;
	else
		n = n2;
	for (i = 0; i < n; ++i) {
		if (z1[i] < z2[i])
			return -1;
		else if(z1[i] > z2[i])
			return 1;
	}
	if (n1 < n2)
		return -1;
	else if(n1 > n2)
		return 1;
	return 0;
}

struct kstr *kstr_new(const char *z, int count) {
	struct kstr *x = NULL;
	if (count < 0)
		count = strlen(z);
	x = gc_alloc(&vm()->gc, sizeof(*x) + count, T_KSTR);
	x->len = count;
	if (!z)
		memset(x->land, 0, count + 1);
	else
		memcpy(x->land, z, count);
	if (z)
		x->hash = kz_hash(x->land, x->len);
	return x;
}

int kstr_equals(const struct kstr *kz, const struct kstr *rhs) {
	if (kz == rhs)
		return 1;
	if (kz->len == rhs->len &&
		kz->hash == rhs->hash)
		return memcmp(kz->land, rhs->land, kz->len) == 0;
	return 0;
}

int kstr_compare(const struct kstr *kz, const struct kstr *rhs) {
	return kz_compare((const unsigned char *)kz->land,
	                  kz->len,
					  (const unsigned char *)rhs->land,
					  rhs->len);
}

//-------------------------------------------------------------------------
// Managed data: `mand` functions:
//-------------------------------------------------------------------------
struct mand *mand_new(const void *data, int size, int (*final)(void *)) {
	struct mand *x = NULL;
	assert(size >= 0);
	x = gc_alloc(&vm()->gc, sizeof(*x) + size, T_MAND);
	x->len = size;
	x->final = final;
	if (data)
		memcpy(x->land, data, size);
	return x;
}

void mand_final(struct mand *pm) {
	int rv = 0;
	if (pm->final)
		rv = (*pm->final)(pm->land);
	if (rv < 0)
		vm_die("Managed data finalize failed.");
}

int mand_equals(const struct mand *pm, const struct mand *rhs) {
	if (pm == rhs)
		return 1;
	if (pm->len == rhs->len && pm->final == rhs->final) {
		if (memcmp(pm->land, rhs->land, pm->len) == 0)
			return 1;
	}
	return 0;
}

int mand_compare(const struct mand *pm, const struct mand *rhs) {
	return kz_compare(pm->land, pm->len, rhs->land, rhs->len);
}
