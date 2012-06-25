#include "state.h"
#include "value.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define MAX_MSG_LEN 1024

static void vm_backtrace(int max);

static void *default_zalloc(struct mach *m, size_t size) {
	void *chunk = calloc(size, 1);
	if (!chunk)
		m->die(m, "System: Not enough memory");
	assert(chunk != NULL);
	return chunk;
}

static void *default_realloc(struct mach *m, void *p, size_t size) {
	void *chunk = realloc(p, size);
	if (!chunk) {
		if (p)
			free(p);
		m->die(m, "Fatal: Realloc failed!");
	}
	assert(chunk != NULL);
	return chunk;
}

static void default_free(struct mach *m, void *chunk) {
	if (!chunk)
		m->die(m, "Fatal: NULL free");
	free(chunk);
}

static void default_die(struct mach *m, const char *msg) {
	UNUSED(m);
	if (!msg || !*msg)
		msg = "Unknown!";
	fprintf(stderr, "== VM Panic!\n%%%% %s\n", msg);
	fprintf(stderr, "-- Back trace:\n");
	vm_backtrace(6);
	longjmp(ioslate()->jpt, 1);
}

static struct mach mach_state = {
	.global  = NULL,
	.zalloc  = default_zalloc,
	.realloc = default_realloc,
	.free    = default_free,
	.die     = default_die,
	.cookie  = NULL,
};

static struct context *owned_ctx = NULL;

struct mach *vm() {
	//assert(mach_state.global != NULL);
	return &mach_state;
}

struct context *ioslate() {
	assert(owned_ctx != NULL);
	return owned_ctx;
}

int vm_init() {
	// Init gc:
	gc_init(&mach_state.gc, 1024);
	// Init global map:
	mach_state.global = hmap_new(-1);
	mach_state.kpool = hmap_new(-1);
	mach_state.gc.root = mach_state.global;
	// Load symbols

	return 0;
}

void vm_final() {
	if (mach_state.fn)
		vm_free(mach_state.fn);
	gc_final(&mach_state.gc);
}

int vm_init_context() {
	owned_ctx = vm_zalloc(sizeof(*owned_ctx));
	//owned_ctx->run = func_new(NULL);
	owned_ctx->top = owned_ctx->stk;
	return 0;
}

void vm_final_context() {
	vm_free(owned_ctx);
	owned_ctx = NULL;
}

static void vm_backtrace(int max) {
	struct call_info *i = ioslate()->info;
	assert(max >= 0);
	while (i && max--) {
		fprintf(stderr, " > %s\n", i->run->proto->land);
		i = i->chain;
	}
	if (i != NULL)
		fprintf(stderr, " > ... more calls ...\n");
}

//------------------------------------------------------------------------
// Generic mapping functions:
// -----------------------------------------------------------------------
struct variable *ymd_put(struct variable *var,
                         const struct variable *key) {
	if (is_nil(key))
		vm_die("No any key will be `nil`");
	switch (var->type) {
	case T_SKLS:
		return skls_put(skls_x(var), key);
	case T_HMAP:
		return hmap_put(hmap_x(var), key);
	case T_DYAY:
		return dyay_get(dyay_x(var), int_of(key));
	default:
		vm_die("Variable can not be put");
		break;
	}
	return NULL;
}

struct variable *ymd_get(struct variable *var,
                         const struct variable *key) {
	if (is_nil(key))
		vm_die("No any key will be `nil`");
	switch (var->type) {
	case T_SKLS:
		return skls_get(skls_x(var), key);
	case T_HMAP:
		return hmap_get(hmap_x(var), key);
	case T_DYAY:
		return dyay_get(dyay_x(var), int_of(key));
	default:
		vm_die("Variable can not be index");
		break;
	}
	return NULL;
}

struct kstr *ymd_strcat(const struct kstr *lhs, const struct kstr *rhs) {
	char *tmp = vm_zalloc(lhs->len + rhs->len);
	struct kvi *x;
	memcpy(tmp, lhs->land, lhs->len);
	memcpy(tmp + lhs->len, rhs->land, rhs->len);
	x = kz_index(vm()->kpool, tmp, lhs->len + rhs->len);
	vm_free(tmp);
	return kstr_x(&x->k);
}

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct func *ymd_spawnf(struct chunk *blk, const char *name,
                        unsigned short *id) {
	vm()->fn = mm_need(vm()->fn, vm()->n_fn, FUNC_ALIGN,
	                   sizeof(struct func*));
	*id = vm()->n_fn;
	vm()->fn[vm()->n_fn++] = func_new(blk, name);
	return vm()->fn[vm()->n_fn - 1];
}

struct kstr *ymd_format(const char *fmt, ...) {
	va_list ap;
	int rv;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	rv = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return ymd_kstr(buf, -1);
}

//-----------------------------------------------------------------------------
// Mach:
// ----------------------------------------------------------------------------
void vm_die(const char *fmt, ...) {
	va_list ap;
	int rv;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	rv = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return vm()->die(vm(), buf);
}
