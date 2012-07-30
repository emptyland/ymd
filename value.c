#include "value.h"
#include "memory.h"
#include "state.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

struct typeof_z {
	int len;
	const char *name;
};
static const struct typeof_z typeof_name[] = {
	{ 3, "nil", },
	{ 3, "int", },
	{ 4, "bool", },
	{ 4, "lite", },
	{ 6, "string", },
	{ 8, "function", },
	{ 5, "array", },
	{ 7, "hashmap", },
	{ 8, "skiplist", },
	{ 7, "managed", },
};

const char *typeof_kz(unsigned tt) {
	assert(tt < sizeof(typeof_name)/sizeof(typeof_name[0]));
	return typeof_name[tt].name;
}

struct kstr *typeof_kstr(struct ymd_mach *vm, unsigned tt) {
	assert(tt < sizeof(typeof_name)/sizeof(typeof_name[0]));
	return kstr_fetch(vm, typeof_name[tt].name, typeof_name[tt].len);
}

//-------------------------------------------------------------------------
// Constant variable value:
//-------------------------------------------------------------------------
static struct variable knil_fake_var = {
	.value = { .i = 0LL, },
	.type = T_NIL,
};
struct variable *knil = &knil_fake_var;

//-------------------------------------------------------------------------
// Type casting define:
//-------------------------------------------------------------------------
ymd_int_t int_of(struct ymd_mach *vm, const struct variable *var) {
	if (var->type != T_INT)
		vm_die(vm, "Variable is not `int`");
	return var->value.i;
}

ymd_int_t bool_of(struct ymd_mach *vm, const struct variable *var) {
	if (var->type != T_BOOL)
		vm_die(vm, "Variable is not `bool`");
	return var->value.i;
}

#define DEFINE_REFOF(name, tt)                   \
struct name *name##_of(struct ymd_mach *vm,      \
		struct variable *var) {                  \
	if (var->type != tt)                         \
		vm_die(vm, "Variable is not `"#name"`"); \
	assert(var->value.ref != NULL);              \
	return (struct name *)var->value.ref;        \
}
DECL_TREF(DEFINE_REFOF)
#undef DEFINE_REFOF

void *mand_land(struct ymd_mach *vm, struct variable *var, const char *tt) {
	struct mand *m = mand_of(vm, var);
	if (tt == m->tt || strcmp(tt, m->tt) == 0)
		return m->land;
	vm_die(vm, "Unexpected managed type `%s`", tt);
	return NULL;
}

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
	case T_FUNC:
		return func_k(lhs) == func_k(rhs); // FIXME:
	case T_EXT:
		return lhs->value.ext == rhs->value.ext;
	case T_DYAY:
		return dyay_equals(dyay_k(lhs), dyay_k(rhs));
	case T_HMAP:
		return hmap_equals(hmap_k(lhs), hmap_k(rhs));
	case T_SKLS:
		return skls_equals(skls_k(lhs), skls_k(rhs));
	//case T_MAND:
	// TODO:
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

int kz_compare(const unsigned char *z1, int n1,
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

//-------------------------------------------------------------------------
// Managed data: `mand` functions:
//-------------------------------------------------------------------------
struct mand *mand_new(struct ymd_mach *vm, int size, ymd_final_t final) {
	struct mand *x = NULL;
	assert(size >= 0);
	x = gc_new(vm, sizeof(*x) + size, T_MAND);
	x->len = size;
	x->final = final;
	return x;
}

void mand_final(struct ymd_mach *vm, struct mand *o) {
	int rv = 0;
	if (o->final)
		rv = (*o->final)(o->land);
	if (rv < 0)
		vm_die(vm, "Managed data finalize failed.");
}

int mand_equals(const struct mand *o, const struct mand *rhs) {
	if (o == rhs)
		return 1;
	if (o->len == rhs->len && o->final == rhs->final) {
		if (memcmp(o->land, rhs->land, o->len) == 0)
			return 1;
	}
	return 0;
}

int mand_compare(const struct mand *o, const struct mand *rhs) {
	return kz_compare(o->land, o->len, rhs->land, rhs->len);
}

struct variable *mand_get(struct mand *o, const struct variable *k) {
	if (!o->proto)
		return knil;
	assert (o->proto->type == T_HMAP || o->proto->type == T_SKLS);
	switch (o->proto->type) {
	case T_HMAP:
		return hmap_get(hmap_f(o->proto), k);
	case T_SKLS:
		return skls_get(skls_f(o->proto), k);
	}
	return knil;
}

struct variable *mand_put(struct ymd_mach *vm, struct mand *o,
                          const struct variable *k) {
	if (!o->proto)
		return knil;
	assert (o->proto->type == T_HMAP || o->proto->type == T_SKLS);
	switch (o->proto->type) {
	case T_HMAP:
		return hmap_put(vm, hmap_f(o->proto), k);
	case T_SKLS:
		return skls_put(vm, skls_f(o->proto), k);
	}
	return knil;
}

