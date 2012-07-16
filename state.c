#include "state.h"
#include "value.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define MAX_MSG_LEN 1024

static void vm_backtrace(struct ymd_context *l, int max);

static void *default_zalloc(struct ymd_mach *m, size_t size) {
	void *chunk = calloc(size, 1);
	if (!chunk)
		m->die(m, "System: Not enough memory");
	assert(chunk != NULL);
	return chunk;
}

static void *default_realloc(struct ymd_mach *m, void *p, size_t size) {
	void *chunk = realloc(p, size);
	if (!chunk) {
		if (p)
			free(p);
		m->die(m, "Fatal: Realloc failed!");
	}
	assert(chunk != NULL);
	return chunk;
}

static void default_free(struct ymd_mach *m, void *chunk) {
	if (!chunk)
		m->die(m, "Fatal: NULL free");
	free(chunk);
}

static void default_die(struct ymd_mach *vm, const char *msg) {
	struct ymd_context *l = ioslate(vm);
	if (!msg || !*msg)
		msg = "Unknown!";
	fprintf(stderr, "== VM Panic!\n%%%% %s\n", msg);
	fprintf(stderr, "-- Back trace:\n");
	vm_backtrace(l, 6);
	longjmp(l->jpt, 1);
}

struct ymd_context *ioslate(struct ymd_mach *vm) {
	assert(vm->curr != NULL);
	return vm->curr;
}

struct ymd_mach *ymd_init() {
	struct ymd_mach *vm = calloc(sizeof(*vm), 1);
	if (!vm)
		return NULL;
	// Basic memory functions:
	vm->zalloc  = default_zalloc;
	vm->realloc = default_realloc;
	vm->free    = default_free;
	vm->die     = default_die;
	// Init gc:
	gc_init(vm, 1024);
	// Init global map:
	vm->global = hmap_new(vm, -1);
	vm->kpool = hmap_new(vm, -1);
	vm->gc.root = vm->global;
	// Load symbols

	return vm;
}

void ymd_final(struct ymd_mach *vm) {
	if (vm->fn)
		vm_free(vm, vm->fn);
	gc_final(vm);
}

int ymd_init_context(struct ymd_mach *vm) {
	vm->curr = vm_zalloc(vm, sizeof(*vm->curr));
	vm->curr->vm = vm;
	//owned_ctx->run = func_new(NULL);
	vm->curr->top = vm->curr->stk;
	return 0;
}

void ymd_final_context(struct ymd_mach *vm) {
	vm_free(vm, vm->curr);
	vm->curr = NULL;
}

static void vm_backtrace(struct ymd_context *l, int max) {
	struct call_info *i = l->info;
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
struct variable *ymd_put(struct ymd_mach *vm,
                         struct variable *var,
                         const struct variable *key) {
	if (is_nil(key))
		vm_die(vm, "No any key will be `nil`");
	switch (var->type) {
	case T_SKLS:
		return skls_put(vm, skls_x(var), key);
	case T_HMAP:
		return hmap_put(vm, hmap_x(var), key);
	case T_DYAY:
		return dyay_get(dyay_x(var), int_of(vm, key));
	default:
		vm_die(vm, "Variable can not be put");
		break;
	}
	return NULL;
}

struct variable *ymd_get(struct ymd_mach *vm,
                         struct variable *var,
                         const struct variable *key) {
	if (is_nil(key))
		vm_die(vm, "No any key will be `nil`");
	switch (var->type) {
	case T_SKLS:
		return skls_get(skls_x(var), key);
	case T_HMAP:
		return hmap_get(hmap_x(var), key);
	case T_DYAY:
		return dyay_get(dyay_x(var), int_of(vm, key));
	default:
		vm_die(vm, "Variable can not be index");
		break;
	}
	return NULL;
}

struct kstr *ymd_strcat(struct ymd_mach *vm, const struct kstr *lhs, const struct kstr *rhs) {
	char *tmp = vm_zalloc(vm, lhs->len + rhs->len);
	struct kvi *x;
	memcpy(tmp, lhs->land, lhs->len);
	memcpy(tmp + lhs->len, rhs->land, rhs->len);
	x = kz_index(vm, vm->kpool, tmp, lhs->len + rhs->len);
	vm_free(vm, tmp);
	return kstr_x(&x->k);
}

struct variable *ymd_def(struct ymd_mach *vm, void *o, const char *field) {
	struct variable k;
	switch (gcx(o)->type) {
	case T_HMAP:
		vset_kstr(&k, ymd_kstr(vm, field, -1));
		return hmap_put(vm, o, &k);
	case T_SKLS:
		vset_kstr(&k, ymd_kstr(vm, field, -1));
		return skls_put(vm, o, &k);
	default:
		assert(0);
		break;
	}
	return NULL;
}

struct variable *ymd_mem(struct ymd_mach *vm, void *o, const char *field) {
	struct variable k;
	switch (gcx(o)->type) {
	case T_HMAP:
		vset_kstr(&k, ymd_kstr(vm, field, -1));
		return hmap_get(o, &k);
	case T_SKLS:
		vset_kstr(&k, ymd_kstr(vm, field, -1));
		return skls_get(o, &k);
	default:
		assert(0);
		break;
	}
	return NULL;
}

struct variable *ymd_getg(struct ymd_mach *vm, const char *field) {
	struct variable k;
	vset_kstr(&k, ymd_kstr(vm, field, -1));
	return hmap_get(vm->global, &k);
}

struct variable *ymd_putg(struct ymd_mach *vm, const char *field) {
	struct variable k;
	vset_kstr(&k, ymd_kstr(vm, field, -1));
	return hmap_put(vm, vm->global, &k);
}

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct kstr *ymd_kstr(struct ymd_mach *vm, const char *z, int n) {
	struct kvi *x;
	size_t lzn = n < 0 ? strlen(z) : n;
	if (lzn > MAX_KPOOL_LEN)
		return kstr_new(vm, z, lzn);
	x = kz_index(vm, vm->kpool, z, n);
	x->v.type = T_INT;
	x->v.value.i++; // Value field is used counter
	return kstr_x(&x->k);
}

struct func *ymd_spawnf(struct ymd_mach *vm,
                        struct chunk *blk,
						const char *name,
                        unsigned short *id) {
	vm->fn = mm_need(vm, vm->fn, vm->n_fn, FUNC_ALIGN,
	                 sizeof(struct func*));
	*id = vm->n_fn;
	vm->fn[vm->n_fn++] = func_new(vm, blk, name);
	return vm->fn[vm->n_fn - 1];
}

struct kstr *ymd_format(struct ymd_mach *vm, const char *fmt, ...) {
	va_list ap;
	int rv;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	rv = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return ymd_kstr(vm, buf, -1);
}

//-----------------------------------------------------------------------------
// Mach:
// ----------------------------------------------------------------------------
void vm_die(struct ymd_mach *vm, const char *fmt, ...) {
	va_list ap;
	int rv;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	rv = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return vm->die(vm, buf);
}
