#ifndef YMD_STATE_H
#define YMD_STATE_H

#include "memory.h"
#include "value.h"
#include <string.h>
#include <setjmp.h>
#include <assert.h>

struct ymd_context;

struct ymd_mach {
	struct hmap *global;
	struct hmap *kpool;
	struct gc_struct gc;
	struct func **fn;
	unsigned short n_fn;
	// Internal memory function:
	void *(*zalloc)(struct ymd_mach *, size_t);
	void *(*realloc)(struct ymd_mach *, void *, size_t);
	void (*free)(struct ymd_mach *, void *);
	void (*die)(struct ymd_mach *, const char *);
	void *cookie;
	struct ymd_context *curr;
	struct variable knil; // nil flag
};

struct ymd_context *ioslate(struct ymd_mach *vm);

struct ymd_mach *ymd_init();
void ymd_final(struct ymd_mach *vm);

int ymd_init_context(struct ymd_mach *vm);
void ymd_final_context(struct ymd_mach *vm);

// Mach functions:
static inline void *vm_zalloc(struct ymd_mach *vm, size_t size) {
	return vm->zalloc(vm, size);
}
static inline void *vm_realloc(struct ymd_mach *vm, void *p, size_t size) {
	return vm->realloc(vm, p, size);
}
static inline void vm_free(struct ymd_mach *vm, void *p) {
	vm->free(vm, p);
}
void vm_die(struct ymd_mach *vm, const char *fmt, ...);

#define UNUSED(useless) ((void)useless)

#define MAX_KPOOL_LEN 40
#define MAX_LOCAL 1024
#define MAX_STACK 128
#define FUNC_ALIGN 128

struct call_info {
	struct call_info *chain;
	struct func *run;
	int pc;
	struct variable *loc;
};

struct ymd_context {
	struct call_info *info;
	struct variable loc[MAX_LOCAL];
	struct variable stk[MAX_STACK];
	struct variable *top;
	jmp_buf jpt; // Jump point
	struct ymd_mach *vm;
};

//-----------------------------------------------------------------------------
// For debug and testing:
// ----------------------------------------------------------------------------
struct call_info *vm_nearcall(struct ymd_context *l);

static inline const char *vm_file(const struct call_info *i) {
	assert(i != NULL);
	return i->run->u.core->file->land;
}

static inline int vm_line(const struct call_info *i) {
	assert(i != NULL);
	return i->run->u.core->line[i->pc - 1];
}

int vm_reached(struct ymd_mach *vm, const char *name);

//-----------------------------------------------------------------------------
// Call and run:
// ----------------------------------------------------------------------------
int ymd_call(struct ymd_context *l, struct func *fn, int argc, int method);

int ymd_main(struct ymd_mach *vm, struct func *fn, int argc, char *argv[]);

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct func *ymd_spawnf(struct ymd_mach *vm, struct chunk *blk,
                        const char *name, unsigned short *id);

//-----------------------------------------------------------------------------
// Misc:
// ----------------------------------------------------------------------------
// Get/Put a container: hashmap/skiplist/array
struct variable *ymd_put(struct ymd_mach *vm,
                         struct variable *var,
                         const struct variable *key);

struct variable *ymd_get(struct ymd_mach *vm,
                         struct variable *var,
                         const struct variable *key);

// Get/Put global variable
struct variable *ymd_putg(struct ymd_mach *vm, const char *field);

struct variable *ymd_getg(struct ymd_mach *vm, const char *field);

// Define/Get object's member
struct variable *ymd_def(struct ymd_mach *vm, void *o, const char *field);

struct variable *ymd_mem(struct ymd_mach *vm, void *o, const char *field);

struct kstr *ymd_kstr(struct ymd_mach *vm, const char *z, int n);

struct kstr *ymd_strcat(struct ymd_mach *vm, const struct kstr *lhs, const struct kstr *rhs);

struct kstr *ymd_format(struct ymd_mach *vm, const char *fmt, ...);

static inline struct func *ymd_called(struct ymd_context *l) {
	assert(l->info);
	assert(l->info->run);
	return l->info->run;
}

static inline struct dyay *ymd_argv(struct ymd_context *l) {
	return ymd_called(l)->argv;
}

static inline struct dyay *ymd_argv_chk(struct ymd_context *l, int need) {
	if (need > 0 && (!ymd_argv(l) || ymd_argv(l)->count < need))
		vm_die(l->vm, "Bad argument, need > %d", need);
	return ymd_argv(l);
}

static inline struct variable *ymd_argv_get(struct ymd_context *l, int i) {
	struct dyay *argv = ymd_argv_chk(l, i + 1);
	return argv->elem + i;
}

static inline struct variable *ymd_bval(struct ymd_context *l, int i) {
	assert(i >= 0);
	assert(i < ymd_called(l)->n_bind);
	return ymd_called(l)->bind + i;
}

//-----------------------------------------------------------------------------
// Stack functions:
// ----------------------------------------------------------------------------
static inline struct variable *ymd_push(struct ymd_context *l) {
	assert(l->top < l->stk + MAX_STACK); // "Stack overflow!"
	++l->top;
	return l->top - 1;
}

static inline struct variable *ymd_top(struct ymd_context *l, int i) {
	assert(l->top != l->stk); // "Stack empty!"
	assert(i >= 0 || 1 - i < l->top - l->stk); // "Stack out of range"
	assert(i <= 0 ||     i < l->top - l->stk); // "Stack out of range"
	return i < 0 ? l->stk + 1 - i : l->top - 1 - i;
}

static inline void ymd_pop(struct ymd_context *l, int n) {
	assert(n <= 0 || l->top != l->stk); // "Stack empty!"
	assert(n <= l->top - l->stk); // "Bad pop"
	l->top -= n;
	memset(l->top, 0, sizeof(*l->top) * n);
}

#endif // YMD_STATE_H

