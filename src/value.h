#ifndef YMD_VALUE_H
#define YMD_VALUE_H

#include "memory.h"
#include "builtin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

//-----------------------------------------------------------------------
// Type defines:
//-----------------------------------------------------------------------
#define T_NIL     0
#define T_INT     1
#define T_FLOAT   2
#define T_BOOL    3
#define T_EXT     4 // Naked pointer
// -->
#define T_REF     5 /* Flag the follow type are gc type: */
// <--
#define T_KSTR    5 // Constant string
#define T_FUNC    6 // Closure
#define T_DYAY    7 // Dynamic array
#define T_HMAP    8 // Hash map
#define T_SKLS    9 // Skip list
#define T_MAND   10 // Managed data(from C/C++)

#define T_MAX    11

#define DECL_TREF(v) \
	v(func, T_FUNC)  \
	v(kstr, T_KSTR)  \
	v(dyay, T_DYAY)  \
	v(hmap, T_HMAP)  \
	v(skls, T_SKLS)  \
	v(mand, T_MAND)

#define MAX_CHUNK_LEN 512

struct variable {
	unsigned char tt;
	union {
		struct gc_node *ref;
		void *ext;
		ymd_float_t f;
		ymd_int_t i;
	} u;
};

// Byte Function
struct chunk {
	int ref; // Reference counter
	struct chunk *chain; // Chain for compiling
	ymd_inst_t *inst; // Instructions
	int *line; // Instruction-line mapping
	int kinst; // Number of instructions
	struct variable *kval; // Constant values
	struct kstr **lz; // Local variable mapping
	struct kstr **uz; // Upval variable mapping
	struct kstr *file; // File name in complied or null
	unsigned short kkval;
	unsigned short klz;
	unsigned short kuz;
	unsigned short kargs; // Prototype number of arguments
};

struct func {
	GC_HEAD;
	struct kstr *name; // Function name
	struct variable *upval; // Up values
	struct dyay *argv;  // Arguments
	unsigned short is_c; // Is native C function?
	unsigned short n_upval; // Natvie function use it for upval;
	union {
		ymd_nafn_t nafn;  // Native function
		struct chunk *core; // Byte code
	} u;
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
	int count;
	int max;
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

#define SKLS_ASC  ((struct func *)0)
#define SKLS_DASC ((struct func *)1)

struct skls {
	GC_HEAD;
	int count;
	unsigned short lv;
	struct func *cmp; // user defined function
	                  // (void*)0 : order by asc
					  // (void*)1 : order by dasc
					  // other    : order by user function
	struct sknd *head;
};

// Managed data (must be from C/C++)
struct mand {
	GC_HEAD;
	int len; // land length
	const char *tt; // Type name
	ymd_final_t final; // Release function, call in deleted
	struct gc_node *proto; // metatable
	unsigned char land[1]; // Payload data
};

// A flag fake variable:
extern struct variable *knil;

// Safe casting
ymd_int_t int_of(struct ymd_context *l, const struct variable *var);
ymd_int_t int4of(struct ymd_context *l, const struct variable *var);
ymd_float_t float_of(struct ymd_context *l, const struct variable *var);
ymd_float_t float4(const struct variable *var);
ymd_float_t float4of(struct ymd_context *l, const struct variable *var);
ymd_int_t bool_of(struct ymd_context *l, const struct variable *var);

// Safe get mand type's payload data pointer
void *mand_land(struct ymd_context *l, struct variable *var,
		const char *tt);

// Literal type strings
const char *typeof_kz(int tt);
struct kstr *typeof_kstr(struct ymd_mach *vm, int tt);

// Generic comparing
int equals(const struct variable *lhs, const struct variable *rhs);
int compare(const struct variable *lhs, const struct variable *rhs);

// Number type comparing
int num_compare(const struct variable *lhs, const struct variable *rhs);

// Internal comparing
int kstr_equals(const struct kstr *kz, const struct kstr *rhs);
int kstr_compare(const struct kstr *kz, const struct kstr *rhs);
int hmap_equals(const struct hmap *o, const struct hmap *rhs);
int hmap_compare(const struct hmap *o, const struct hmap *rhs);
int skls_equals(const struct skls *o, const struct skls *rhs);
int skls_compare(const struct skls *o, const struct skls *rhs);
int dyay_equals(const struct dyay *o, const struct dyay *rhs);
int dyay_compare(const struct dyay *o, const struct dyay *rhs);
int mand_equals(const struct mand *o, const struct mand *rhs);
int mand_compare(const struct mand *o, const struct mand *rhs);

// Constant string: `kstr`
struct kstr *kstr_fetch(struct ymd_mach *vm, const char *z, int count);

size_t kz_hash(const char *z, int n);

int kz_compare(const unsigned char *z1, int n1,
               const unsigned char *z2, int n2);

static inline size_t kstr_hash(struct kstr *kz) {
	kz->hash = kz->hash ? kz->hash : kz_hash(kz->land, kz->len);
	return kz->hash;
}

void kpool_init(struct ymd_mach *vm);
void kpool_final(struct ymd_mach *vm);

// Hash map: `hmap` functions:
struct hmap *hmap_new(struct ymd_mach *vm, int count);
void hmap_final(struct ymd_mach *vm, struct hmap *o);
struct variable *hmap_put(struct ymd_mach *vm, struct hmap *o,
                          const struct variable *key);
struct variable *hmap_get(struct hmap *o, const struct variable *k);
int hmap_remove(struct ymd_mach *vm, struct hmap *o,
                const struct variable *k);

// Skip list: `skls` functions:
// order: SKLS_ASC  -> order by asc
//        SKLS_DASC -> order by dasc
//        func      -> user defined
struct skls *skls_new(struct ymd_mach *vm, struct func *order);
void skls_final(struct ymd_mach *vm, struct skls *o);
struct variable *skls_put(struct ymd_mach *vm, struct skls *o,
		const struct variable *k);
struct variable *skls_get(struct ymd_mach *vm, struct skls *o,
		const struct variable *k);
int skls_remove(struct ymd_mach *vm, struct skls *o,
		const struct variable *k);

// Compare the skip list's key only.
ymd_int_t skls_key_compare(struct ymd_mach *vm, const struct skls *o,
		const struct variable *lhs, const struct variable *rhs);

// Retrun first great or equal than key's node.
struct sknd *skls_direct(struct ymd_mach *vm, const struct skls *o,
		const struct variable *k);

// Dynamic array: `dyay` functions:
struct dyay *dyay_new(struct ymd_mach *vm, int count);
void dyay_final(struct ymd_mach *vm, struct dyay *o);
struct variable *dyay_get(struct dyay *o, ymd_int_t i);
struct variable *dyay_add(struct ymd_mach *vm, struct dyay *o);
struct variable *dyay_insert(struct ymd_mach *vm, struct dyay *o,
	                         ymd_int_t i);
int dyay_remove(struct ymd_mach *vm, struct dyay *o, ymd_int_t i);

// Managed data: `mand` functions:
struct mand *mand_new(struct ymd_mach *vm, int size, ymd_final_t final);
void mand_final(struct ymd_mach *vm, struct mand *o);

// Proxy getting and putting
struct variable *mand_get(struct ymd_mach *vm, struct mand *o,
		const struct variable *k);
struct variable *mand_put(struct ymd_mach *vm, struct mand *o,
		const struct variable *k);
int mand_remove(struct ymd_mach *vm, struct mand *o,
		const struct variable *k);

// Chunk and compiling:
void blk_final(struct ymd_mach *vm, struct chunk *core);
int blk_emit(struct ymd_mach *vm, struct chunk *core, ymd_inst_t inst,
             int line);

// Find or get `string' in constant list
int blk_kz(struct ymd_mach *vm, struct chunk *core, const char *z,
           int k);

// Find or put `int' in constant list
int blk_ki(struct ymd_mach *vm, struct chunk *core, ymd_int_t i);

// Find or put `float' in constant list
int blk_kd(struct ymd_mach *vm, struct chunk *core, ymd_float_t f);

// Find or put `function' in constant list
int blk_kf(struct ymd_mach *vm, struct chunk *core, void *fn);

int blk_find_lz(struct chunk *core, const char *z);
int blk_add_lz(struct ymd_mach *vm, struct chunk *core, const char *z);
int blk_find_uz(struct chunk *core, const char *z);
int blk_add_uz(struct ymd_mach *vm, struct chunk *core, const char *z);
void blk_shrink(struct ymd_mach *vm, struct chunk *core);

// Closure functions:
struct func *func_new(struct ymd_mach *vm, struct chunk *blk,
                      const char *name);
struct func *func_new_c(struct ymd_mach *vm, ymd_nafn_t nafn,
                        const char *name);
struct func *func_clone(struct ymd_mach *vm, struct func *fn);
void func_final(struct ymd_mach *vm, struct func *fn);

// Return the func's prototype, like this:
// func len(...) {[native:0x7864]}
// func foo(a, b, c)
struct kstr *func_proto(struct ymd_mach *vm, struct func *fn);

const char *func_proto_z(const struct func *fn, char *buf, size_t len);

// Return a variable for binding, default: nil
struct variable *func_bind(struct ymd_mach *vm, struct func *fn, int i);

// Dump a function debug informations.
void func_dump(struct func *fn, FILE *fp);

#endif // YMD_VALUE_H
