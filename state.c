#include "state.h"
#include "value.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

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
	fprintf(stderr, "== VM Panic!\n -%s\n", msg);
	exit(0);
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
	gc_final(&mach_state.gc);
}

int vm_init_context() {
	owned_ctx = vm_zalloc(sizeof(*owned_ctx));
	owned_ctx->run = func_new(NULL);
	owned_ctx->top = owned_ctx->stk;
	return 0;
}

void vm_final_context() {
	vm_free(owned_ctx);
	owned_ctx = NULL;
}

//------------------------------------------------------------------------
// Generic mapping functions:
// -----------------------------------------------------------------------
struct variable *ymd_get(struct variable *var, const struct variable *key) {
	switch (var->type) {
	case T_SKLS:
		return skls_get(skls_x(var), key);
	case T_HMAP:
		return hmap_get(hmap_x(var), key);
	default:
		vm_die("Variable is not a mapping container");
		break;
	}
	return knax;
}

/*
struct variable *ymd_getz(struct variable *var, const char *z, int n) {
	switch (var->type) {
	case T_SKLS:
		return skls_getz(skls_x(var), z, n);
	case T_HMAP:
		return hmap_getz(hmap_x(var), z, n);
	default:
		vm_die("Variable is not a mapping container");
		break;
	}
	return knax;
}
*/
