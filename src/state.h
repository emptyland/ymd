#ifndef YMD_STATE_H
#define YMD_STATE_H

#include "memory.h"
#include "value.h"
#include "value_inl.h"
#include <string.h>
#include <setjmp.h>
#include <assert.h>

struct ymd_context;
struct ymd_mach;

// Constant string pool
struct kpool {
	struct gc_node **slot;
	int used;
	int shift;
};

struct ymd_mach {
	struct kpool kpool; // String pool
	struct gc_struct gc; // GC
	// Internal memory function:
	void *(*zalloc)(struct ymd_mach *, void *, size_t);
	void (*free)(struct ymd_mach *, void *);
	void *cookie; // Cookie for allocator
	ymd_int_t tick; // Instruction tick
	int fatal; // Has panic? 
	struct hmap *global; // Global environment
	struct ymd_context *curr; // Current context
	void *pcre_js; // pcre jit stack
	struct variable knil; // nil flag
};

struct ymd_mach *ymd_init();

void ymd_final(struct ymd_mach *vm);

#define ioslate(vm) ((vm)->curr)

static YMD_INLINE void ymd_log4gc(struct ymd_mach *vm, FILE *logf) {
	vm->gc.logf = logf;
}

// Mach functions:
static YMD_INLINE void *vm_zalloc(struct ymd_mach *vm, size_t size) {
	return vm->zalloc(vm, NULL, size);
}
static YMD_INLINE void *vm_realloc(struct ymd_mach *vm, void *p, size_t size) {
	return vm->zalloc(vm, p, size);
}
static YMD_INLINE void vm_free(struct ymd_mach *vm, void *p) {
	vm->free(vm, p);
}

#define UNUSED(useless) ((void)useless)

#define MAX_KPOOL_LEN 40
#define MAX_LOCAL     512
#define FUNC_ALIGN    128
#define GC_THESHOLD   10240

// Config for stack size
#define YMD_INIT_STACK 128
#define YMD_MAX_STACK  102400

// Config for PCRE jit stack:
#define YMD_JS_START 1024
#define YMD_JS_MAX   4096

struct call_info {
	struct call_info *chain;
	struct func *run; // Current function
	union {
		struct dyay *argv; // Argv object, use in ymd function.
		size_t frame; // Args stack frame, use in C function.
	} u;
	short argc; // Current calling's argc
	short adjust; // Adjust for argc
	int pc;
	struct variable *loc;
};

struct call_jmpbuf {
	struct call_jmpbuf *chain;
	int level;
	int panic;
	jmp_buf core; // ANSI-C jmp_buf
};

struct ymd_context {
	struct call_info *info;
	struct variable loc[MAX_LOCAL];
	struct variable *stk;
	struct variable *top;
	size_t kstk;
	struct call_jmpbuf *jpt; // Jump point
	struct ymd_mach *vm;
};

#define L struct ymd_context *l

//-----------------------------------------------------------------------------
// For debug and testing:
// ----------------------------------------------------------------------------
struct call_info *vm_nearcall(L);

static YMD_INLINE const char *vm_file(const struct call_info *i) {
	assert(i != NULL);
	assert(i->run);
	assert(!i->run->is_c);
	return i->run->u.core->file->land;
}

static YMD_INLINE int vm_line(const struct call_info *i) {
	assert(i != NULL);
	assert(i->run);
	assert(!i->run->is_c);
	return i->run->u.core->line[i->pc - 1];
}

int vm_loaded(struct ymd_mach *vm, const char *name);

//-----------------------------------------------------------------------------
// Call and run:
// ----------------------------------------------------------------------------
YMD_NORETURN void ymd_panic(L, const char *fmt, ...);

YMD_NORETURN void ymd_raise(L);

int ymd_call(L, struct func *fn, int argc, int method);

// Internal protected call
int ymd_pcall(L, struct func *fn, int argc);

// External protected call
int ymd_xcall(L, int argc);

int ymd_ncall(L, struct func *fn, int nret, int narg);
int ymd_main(L, int argc, char *argv[]);


//-----------------------------------------------------------------------------
// Misc:
// ----------------------------------------------------------------------------
// Get/Put/Del a container: hashmap/skiplist/array
struct variable *vm_put(struct ymd_mach *vm, struct variable *var,
                        const struct variable *key);

struct variable *vm_get(struct ymd_mach *vm, struct variable *var,
                        const struct variable *key);

int vm_remove(struct ymd_mach *vm, struct variable *var,
              const struct variable *key);

// Get/Put global variable
struct variable *vm_putg(struct ymd_mach *vm, const char *field);

struct variable *vm_getg(struct ymd_mach *vm, const char *field);


// Define/Get object's member
struct variable *vm_def(struct ymd_mach *vm, void *o, const char *field);

struct variable *vm_mem(struct ymd_mach *vm, void *o, const char *field);

// String tool
struct kstr *vm_strcat(struct ymd_mach *vm, const struct kstr *lhs,
                       const struct kstr *rhs);

struct kstr *vm_format(struct ymd_mach *vm, const char *fmt, ...);

// Comparing:
// lhs == rhs ?
static YMD_INLINE int vm_equals(const struct variable *lhs,
		const struct variable *rhs) {
	return (is_num(lhs) && is_num(rhs)) ? num_compare(lhs, rhs) == 0 :
		equals(lhs, rhs);
}

// Compare lhs and rhs
static YMD_INLINE int vm_compare(const struct variable *lhs,
		const struct variable *rhs) {
	return (is_num(lhs) && is_num(rhs)) ? num_compare(lhs, rhs) :
		compare(lhs, rhs);
}

int vm_bool(const struct variable *lhs);

void vm_pcre_lazy(struct ymd_mach *vm);

//
// Runtime:
//
static YMD_INLINE struct func *ymd_called(L) {
	assert(l->info);
	assert(l->info->run);
	return l->info->run;
}

// Get upval by index, in context.
struct variable *ymd_upval(L, int i);
//-----------------------------------------------------------------------------
// Stack functions:
// ----------------------------------------------------------------------------
void ymd_pop(L, int n);

struct variable *ymd_push(L);

struct variable *ymd_top(L, int i);

static YMD_INLINE size_t ymd_offset(L, int i) {
	struct variable *end = ymd_top(l, i);
	assert (end >= l->stk);
	return end - l->stk;
}

static YMD_INLINE void ymd_move(L, int i) {
	struct variable k, *p = ymd_top(l, i);
	if (i == 0) return;
	k = *p;
	memmove(p, p + 1, (l->top - p - 1) * sizeof(k));
	*ymd_top(l, 0) = k;
}

// Adjust return variables
static YMD_INLINE int ymd_adjust(L, int adjust, int ret) {
	if (adjust < ret) {
		ymd_pop(l, ret - adjust);
		return ret;
	}
	if (adjust > ret) {
		int i = adjust - ret;
		while (i--)
			setv_nil(ymd_push(l));
	}
	return ret;
}

//-----------------------------------------------------------------------------
// Fake stack functions:
// ----------------------------------------------------------------------------
static YMD_INLINE void ymd_dyay(L, int k) {
	struct dyay *o = dyay_new(l->vm, k);
	setv_dyay(ymd_push(l), o); 
}

static YMD_INLINE void ymd_add(L) {
	struct dyay *o = dyay_of(l, ymd_top(l, 1));
	*dyay_add(l->vm, o) = *ymd_top(l, 0);
	ymd_pop(l, 1);
}

static YMD_INLINE void ymd_hmap(L, int k) {
	struct hmap *o = hmap_new(l->vm, k);
	setv_hmap(ymd_push(l), o);
}

static YMD_INLINE void ymd_skls(L, struct func *cmp) {
	struct skls *o = skls_new(l->vm, cmp);
	setv_skls(ymd_push(l), o);
}

static YMD_INLINE void *ymd_mand(L, const char *tt, size_t size,
                             ymd_final_t final) {
	struct mand *o = mand_new(l->vm, size, final);
	o->tt = tt;
	setv_mand(ymd_push(l), o);
	return o->land;
}

static YMD_INLINE void ymd_kstr(L, const char *z, int len) {
	struct kstr *o = kstr_fetch(l->vm, z, len);
	setv_kstr(ymd_push(l), o);
}

void ymd_format(L, const char *fmt, ... );

static YMD_INLINE void ymd_int(L, ymd_int_t i) {
	setv_int(ymd_push(l), i);
}

static YMD_INLINE void ymd_float(L, ymd_float_t f) {
	setv_float(ymd_push(l), f);
}

static YMD_INLINE void ymd_ext(L, void *p) {
	setv_ext(ymd_push(l), p);
}

static YMD_INLINE void ymd_bool(L, int b) {
	setv_bool(ymd_push(l), b);
}

static YMD_INLINE void ymd_nil(L) {
	setv_nil(ymd_push(l));
}

static YMD_INLINE void ymd_nafn(L, ymd_nafn_t fn, const char *name, int nbind) {
	struct func *o = func_new_c(l->vm, fn, name);
	o->n_upval = nbind;
	setv_func(ymd_push(l), o);
}

static YMD_INLINE void ymd_func(L, struct chunk *blk, const char *name) {
	struct func *o = func_new(l->vm, blk, name);
	setv_func(ymd_push(l), o);
}

static YMD_INLINE struct func *ymd_naked(L, struct chunk *blk) {
	struct func *o = func_new(l->vm, blk, NULL);
	setv_func(ymd_push(l), o);
	return o;
}

static YMD_INLINE void ymd_getf(L) {
	struct variable *v = vm_get(l->vm, ymd_top(l, 1), ymd_top(l, 0));
	*ymd_top(l, 0) = *v;
}

static YMD_INLINE void ymd_putf(L) {
	struct variable *v = vm_put(l->vm, ymd_top(l, 2), ymd_top(l, 1));
	*v = *ymd_top(l, 0);
	ymd_pop(l, 2);
}

static YMD_INLINE void ymd_mem(L, const char *field) {
	struct variable *v;
	if (!is_ref(ymd_top(l, 0)))
		ymd_panic(l, "Object must be hashmap or skiplist");
	v = vm_mem(l->vm, ymd_top(l, 0)->u.ref, field);
	*ymd_push(l) = *v;
}

static YMD_INLINE void ymd_def(L, const char *field) {
	if (!is_ref(ymd_top(l, 1)))
		ymd_panic(l, "Object must be hashmap or skiplist");
	*vm_def(l->vm, ymd_top(l, 1)->u.ref, field) = *ymd_top(l, 0);
	ymd_pop(l, 1);
}

static YMD_INLINE void ymd_getg(L, const char *field) {
	*ymd_push(l) = *vm_getg(l->vm, field);
}

static YMD_INLINE void ymd_putg(L, const char *field) {
	*vm_putg(l->vm, field) = *ymd_top(l, 0);
	ymd_pop(l, 1);
}

static YMD_INLINE void ymd_bind(L, int i) {
	struct func *o = func_of(l, ymd_top(l, 1));
	if (i < 0 || i >= o->n_upval)
		ymd_panic(l, "Upval index out of range, %d vs. [%d, %d)",
				i, 0, o->n_upval);
	*func_bind(l->vm, o, i) = *ymd_top(l, 0);
	ymd_pop(l, 1);
}

static YMD_INLINE void ymd_insert(L) {
	struct dyay *o = dyay_of(l, ymd_top(l, 2));
	ymd_int_t i = int_of(l, ymd_top(l, 1));
	*dyay_insert(l->vm, o, i) = *ymd_top(l, 0);
	ymd_pop(l, 2);
}

static YMD_INLINE void ymd_setmetatable(L) {
	struct mand *o = mand_of(l, ymd_top(l, 1));
	if (ymd_type(ymd_top(l, 0)) != T_HMAP && ymd_type(ymd_top(l, 0)) != T_SKLS)
		ymd_panic(l, "Not metatable type!");
	mand_proto(o, ymd_top(l, 0)->u.ref);
	ymd_pop(l, 1);
}

// Push error info in stack! pcall() can use it.
int ymd_error(L, const char *msg);

// Get argc
static YMD_INLINE int ymd_argc(L) {
	assert(!func_argv(ymd_called(l)));
	return l->info->argc;
}

// Gat args
// print (1, 2, 3)
// print [4]
// 1 [3-1 2]
// 2 [3-2 1]
// 3 [3-3 0]
static YMD_INLINE struct variable *ymd_argv(L, int i) {
	int k = ymd_argc(l);
	if (i < 0 || i >= k)
		ymd_panic(l, "Argv index[%d] out of range[0, %d)", i, k);
	return l->stk + l->info->u.frame + i;
}

#undef L
#endif // YMD_STATE_H

