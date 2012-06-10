#ifndef YMD_STATE_H
#define YMD_STATE_H

#include "memory.h"
#include "value.h"
#include <string.h>

struct mach {
	struct hmap *global;
	struct hmap *kpool;
	struct gc_struct gc;
	struct func **fn;
	unsigned short n_fn;
	//lock
	void *(*zalloc)(struct mach *, size_t);
	void *(*realloc)(struct mach *, void *, size_t);
	void (*free)(struct mach *, void *);
	void (*die)(struct mach *, const char *);
	void *cookie;
};

struct mach *vm();
struct context *ioslate();

int vm_init();
void vm_final();

int vm_init_context();
void vm_final_context();

// Mach functions:
static inline void *vm_zalloc(size_t size) {
	return vm()->zalloc(vm(), size);
}
static inline void *vm_realloc(void *p, size_t size) {
	return vm()->realloc(vm(), p, size);
}
static inline void vm_free(void *p) {
	vm()->free(vm(), p);
}
void vm_die(const char *fmt, ...);

#define UNUSED(useless) ((void)useless)
#define IF_DIE(expr, msg) if (expr) vm_die(msg)

#define MAX_LOCAL 1024
#define MAX_STACK 128
#define FUNC_ALIGN 128

struct call_info {
	struct call_info *chain;
	struct func *run;
	int pc;
	struct variable *loc;
};

struct context {
	struct call_info *info;
	struct variable loc[MAX_LOCAL];
	struct variable stk[MAX_STACK];
	struct variable *top;
};

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct func *ymd_spawnf(unsigned short *id);

// ----------------------------------------------------------------------------
struct variable *ymd_get(struct variable *var, const struct variable *key);

static inline struct kstr *ymd_kstr(const char *z, int n) {
	struct kvi *x = kz_index(vm()->kpool, z, n);
	return kstr_x(&x->k);
}

struct kstr *ymd_format(const char *fmt, ...);

//-----------------------------------------------------------------------------
// Stack functions:
// ----------------------------------------------------------------------------
static inline struct variable *ymd_push(struct context *l) {
	IF_DIE(l->top >= l->stk + MAX_STACK, "Stack overflow!");
	++l->top;
	return l->top - 1;
}

static inline struct variable *ymd_top(struct context *l, int i) {
	IF_DIE(l->top == l->stk, "Stack empty!");
	IF_DIE(i < 0 && 1 - i >= l->top - l->stk, "Stack out of range");
	IF_DIE(i > 0 &&     i >= l->top - l->stk, "Stack out of range");
	return i < 0 ? l->stk + 1 - i : l->top - 1 - i;
}

static inline void ymd_pop(struct context *l, int n) {
	IF_DIE(n > 0 && l->top == l->stk, "Stack empty!");
	IF_DIE(n > l->top - l->stk, "Bad pop");
	while (n--) {
		memset(l->top, 0, sizeof(*l->top));
		--l->top;
	}
}

static inline void ymd_push_int(struct context *l, ymd_int_t i) {
	struct variable *v = ymd_push(l);
	v->value.i = i;
	v->type = T_INT;
}

static inline void ymd_push_bool(struct context *l, ymd_int_t i) {
	struct variable *v = ymd_push(l);
	v->value.i = i;
	v->type = T_BOOL;
}

static inline void ymd_push_nil(struct context *l) {
	struct variable *v = ymd_push(l);
	v->type = T_NIL;
}

static inline void ymd_push_kstr(struct context *l, const char *z, int n) {
	struct variable *v = ymd_push(l);
	v->value.ref = (struct gc_node *)ymd_kstr(z, n);
	v->type = T_KSTR;
}

static inline void ymd_push_hmap(struct context *l, int n) {
	struct variable *v = ymd_push(l);
	v->value.ref = (struct gc_node *)hmap_new(n);
	v->type = T_HMAP;
}

static inline void ymd_push_skls(struct context *l) {
	struct variable *v = ymd_push(l);
	v->value.ref = (struct gc_node *)skls_new();
	v->type = T_SKLS;
}

static inline void ymd_push_func(struct context *l) {
	struct variable *v = ymd_push(l);
	v->value.ref = (struct gc_node *)func_new(NULL);
	v->type = T_FUNC;
}

static inline void ymd_setf(struct context *l, int n) {
	int i;
	n <<= 1;
	struct variable *var = ymd_top(l, n);
	for (i = 0; i < n; i += 2) {
		struct variable *v = ymd_get(var, ymd_top(l, i + 1));
		*v = *ymd_top(l, i);
	}
	ymd_pop(l, n + 1);
}

static inline void ymd_index(struct context *l) {
	struct variable *k = ymd_top(l, 0);
	struct variable *v = ymd_get(ymd_top(l, 1), k);
	ymd_pop(l, 2);
	*ymd_push(l) = *v;
}

#endif // YMD_STATE_H
