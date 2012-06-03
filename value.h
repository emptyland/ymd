#ifndef YMD_VALUE_H
#define YMD_VALUE_H

#include "memory.h"
#include <assert.h>
#include <stdio.h>

//-----------------------------------------------------------------------
// Type defines:
//-----------------------------------------------------------------------
#define T_NIL     0
#define T_INT     1
#define T_BOOL    2
#define T_EXT     3 // Naked pointer
#define T_KSTR    4 // Constant string
#define T_FUNC    5 // Closure
#define T_DYAY    6 // Dynamic array
#define T_HMAP    7 // Hash map
#define T_SKLS    8 // Skip list
#define KNAX      9 // Flag value

#define DECL_TREF(v) \
	v(func, T_FUNC)  \
	v(kstr, T_KSTR)  \
	v(dyay, T_DYAY)  \
	v(hmap, T_HMAP)  \
	v(skls, T_SKLS)

#define MAX_CHUNK_LEN 512

struct context;
struct mach;

typedef unsigned long long ymd_int_t;
typedef unsigned int       ymd_inst_t;
typedef int (*ymd_nafn_t)(struct context *);


struct variable {
	union {
		struct gc_node *ref;
		void *ext;
		ymd_int_t i;
	} value;
	unsigned char type;
};

struct func {
	GC_HEAD;
	struct func *chain; // Call chain[==prev]
	struct variable *bind; // Binded values
	struct variable *up; // == this
	ymd_nafn_t nafn;  // Native function
	ymd_inst_t *inst; // Instructions
	int n_inst;
	struct kstr **kz; // Local constant strings
	struct kstr **lz; // Local variable mapping
	struct kstr *proto; // Prototype
	unsigned short n_kz;
	unsigned short n_lz;
	unsigned short n_bind;
};

// Constant String:
struct kstr {
	GC_HEAD;
	int len;
	size_t hash;
	char land[1];
};

// Dynamic Array:
struct dyay {
	GC_HEAD;
	short count;
	unsigned char shift;
	struct variable *elem;
};

// Hash Map:
// Key-value pair:
struct kvi {
	struct kvi *next;
	size_t hash;       // Fast hash value
	struct variable k;
	struct variable v;
	unsigned char flag;
};

struct hmap {
	GC_HEAD;
	int shift;
	struct kvi *item;
	struct kvi *free;
};

// Skip List:
struct sknd {
	struct variable k;
	struct variable v;
	unsigned short n;
	struct sknd *fwd[1]; // Forward list
};

struct skls {
	GC_HEAD;
	int count;
	unsigned short lv;
	struct sknd *head;
};

// A flag fake variable:
extern struct variable *knax;

#define DEFINE_REFCAST(name, tt) \
static inline struct name *name##_x(struct variable *var) { \
	assert(var->value.ref->type == tt); \
	return (struct name *)var->value.ref; \
} \
static inline const struct name *name##_k(const struct variable *var) { \
	assert(var->value.ref->type == tt); \
	return (const struct name *)var->value.ref; \
}
DECL_TREF(DEFINE_REFCAST)
#undef DEFINE_REFCAST

// Generic functions:
static inline int is_nil(const struct variable *v) {
	return v->type == T_NIL;
}
ymd_int_t int_of(struct variable *var);
#define DECL_REFOF(name, tt) \
struct name *name##_of(struct variable *var);
DECL_TREF(DECL_REFOF)
#undef DECL_REFOF

int equal(const struct variable *lhs, const struct variable *rhs);
int compare(const struct variable *lhs, const struct variable *rhs);

// Constant string: `kstr`
struct kstr *kstr_new(const char *z, int n);
int kstr_equal(const struct kstr *kz, const struct kstr *rhs);
int kstr_compare(const struct kstr *kz, const struct kstr *rhs);
size_t kz_hash(const char *z, int n);
struct kvi *kz_index(struct hmap *map, const char *z, int n);

// Hash map: `hmap` functions:
struct hmap *hmap_new(int count);
void hmap_final(struct hmap *map);
int hmap_equal(const struct hmap *map, const struct hmap *rhs);
int hmap_compare(const struct hmap *map, const struct hmap *rhs);
struct variable *hmap_get(struct hmap *map, const struct variable *key);

// Skip list: `skls` functions:
struct skls *skls_new();
void skls_final(struct skls *list);
int skls_equal(const struct skls *list, const struct skls *rhs);
int skls_compare(const struct skls *list, const struct skls *rhs);
struct variable *skls_get(struct skls *list, const struct variable *key);

// Dynamic array: `dyay` functions:
struct dyay *dyay_new(int count);

// Closure functions:
struct func *func_new(ymd_nafn_t nafn);
void func_final(struct func *fn);
int func_emit(struct func *fn, ymd_inst_t inst);
int func_kz(struct func *fn, const char *z, int n);
int func_lz(struct func *fn, const char *z, int n);
int func_find_lz(struct func *fn, const char *z);
int func_add_lz(struct func *fn, const char *z);
int func_bind(struct func *fn, const struct variable *var);
void func_shrink(struct func *fn);
int func_call(struct func *fn, int argc);
struct func *func_compile(FILE *fp);
void func_dump(struct func *fn, FILE *fp);

#endif // YMD_VALUE_H
