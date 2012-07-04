#ifndef YMD_STATE_H
#define YMD_STATE_H

#include "memory.h"
#include "value.h"
#include <string.h>
#include <setjmp.h>

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
	jmp_buf jpt; // Jump point
};

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct func *ymd_spawnf(struct chunk *blk, const char *name, unsigned short *id);

//-----------------------------------------------------------------------------
// Misc:
// ----------------------------------------------------------------------------
struct variable *ymd_put(struct variable *var,
                         const struct variable *key);

struct variable *ymd_get(struct variable *var,
                         const struct variable *key);

static inline struct kstr *ymd_kstr(const char *z, int n) {
	struct kvi *x = kz_index(vm()->kpool, z, n);
	x->v.type = T_INT;
	x->v.value.i++; // Value field is used counter
	return kstr_x(&x->k);
}

struct kstr *ymd_strcat(const struct kstr *lhs, const struct kstr *rhs);

struct kstr *ymd_format(const char *fmt, ...);

static inline struct func *ymd_called(struct context *l) {
	assert(l->info);
	assert(l->info->run);
	return l->info->run;
}

static inline struct dyay *ymd_argv(struct context *l) {
	return ymd_called(l)->argv;
}

static inline struct dyay *ymd_argv_chk(struct context *l, int need) {
	if (need > 0 && (!ymd_argv(l) || ymd_argv(l)->count < need))
		vm_die("Bad argument, need > %d", need);
	return ymd_argv(l);
}

static inline struct variable *ymd_argv_get(struct context *l, int i) {
	struct dyay *argv = ymd_argv_chk(l, i + 1);
	return argv->elem + i;
}

static inline struct variable *ymd_bval(struct context *l, int i) {
	assert(i >= 0);
	assert(i < ymd_called(l)->n_bind);
	return ymd_called(l)->bind + i;
}

struct variable *ymd_def(void *o, const char *field);

struct variable *ymd_mem(void *o, const char *field);

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
	l->top -= n;
	memset(l->top, 0, sizeof(*l->top) * n);
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
	v->value.ref = gcx(ymd_kstr(z, n));
	v->type = T_KSTR;
}

static inline void ymd_push_hmap(struct context *l, struct hmap *map) {
	struct variable *v = ymd_push(l);
	v->value.ref = gcx(map);
	v->type = T_HMAP;
}

static inline void ymd_push_skls(struct context *l, struct skls *list) {
	struct variable *v = ymd_push(l);
	v->value.ref = gcx(list);
	v->type = T_SKLS;
}

static inline void ymd_push_func(struct context *l, struct func *fn) {
	struct variable *v = ymd_push(l);
	v->value.ref = gcx(fn);
	v->type = T_FUNC;
}

#endif // YMD_STATE_H
