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
ymd_int_t int_of(struct variable *var) {
	if (var->type != T_INT)
		vm_die("Variable is not `int`");
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
int equal(const struct variable *lhs, const struct variable *rhs) {
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
		return kstr_equal(kstr_k(lhs), kstr_k(rhs));
	//case T_CLOSURE:
	//	break;
	case T_EXT:
		return lhs->value.ext == rhs->value.ext;
	//case T_DYAY:
	//	break;
	case T_HMAP:
		return hmap_equal(hmap_k(lhs), hmap_k(lhs));
	case T_SKLS:
		return skls_equal(skls_k(rhs), skls_k(lhs));
	default:
		assert(0);
		break;
	}
	return 0;
}

#define safe_compare(r, l) do {\
	if ((r) < (l))      \
		return -1;      \
	else if ((r) > (l)) \
		return 1;       \
	return 0;           \
} while (0)

int compare(const struct variable *lhs, const struct variable *rhs) {
	if (lhs == rhs)
		return 0;
	if (lhs->type != rhs->type) {
		safe_compare(lhs->type, rhs->type);
	}
	switch (lhs->type) {
	case T_NIL:
		return 0;
	case T_INT:
	case T_BOOL:
		safe_compare(lhs->value.i, rhs->value.i);
	case T_KSTR:
		return kstr_compare(kstr_k(lhs), kstr_k(rhs));
	//case T_CLOSURE:
	//	break;
	case T_EXT:
		//safe_compare(lhs->value.ext, rhs->value.ext);
		return 0; // Do not compare a pointer.
	//case T_DYAY:
	//	break;
	case T_HMAP:
		return hmap_compare(hmap_k(lhs), hmap_k(rhs));
	case T_SKLS:
		return skls_compare(skls_k(lhs), skls_k(rhs));
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

int kstr_equal(const struct kstr *kz, const struct kstr *rhs) {
	if (kz == rhs)
		return 1;
	if (kz->len == rhs->len &&
		kz->hash == rhs->hash)
		return memcmp(kz->land, rhs->land, kz->len) == 0;
	return 0;
}

int kstr_compare(const struct kstr *kz, const struct kstr *rhs) {
	int i, n;
	if (kz == rhs)
		return 0;
	if (kz->len <= rhs->len)
		n = kz->len;
	else
		n = rhs->len;
	for (i = 0; i < n; ++i) {
		if (kz->land[i] < rhs->land[i])
			return -1;
		else if(kz->land[i] > rhs->land[i])
			return 1;
	}
	if (kz->len < rhs->len)
		return -1;
	else if(kz->len > rhs->len)
		return 1;
	return 0;
}

