#include "state.h"
#include "value.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define MAX_MSG_LEN 1024

static void vm_backtrace(struct ymd_context *l, int max);
static int vm_init_context(struct ymd_mach *vm);
static void vm_final_context(struct ymd_mach *vm);

static void *default_zalloc(struct ymd_mach *m, void *p, size_t size) {
	void *chunk = !p ? calloc(size, 1) : realloc(p, size);
	if (!chunk) {
		if (p)
			free(p);
		m->die(m, "System: Not enough memory");
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
	struct call_info *i = vm_nearcall(l);
	if (!msg || !*msg)
		msg = "Unknown!";
	if (i)
		fprintf(stderr, "== VM Panic!\n%s:%d %s\n",
				i->run->u.core->file->land,
				i->run->u.core->line[i->pc - 1], msg);
	else
		fprintf(stderr, "== VM Panic!\n%%%% %s\n", msg);
	fprintf(stderr, "-- Back trace:\n");
	vm_backtrace(l, 6);
	longjmp(l->jpt, 1);
}

struct ymd_context *ioslate(struct ymd_mach *vm) {
	assert(vm->curr != NULL);
	return vm->curr;
}

static int vm_init_context(struct ymd_mach *vm) {
	vm->curr = vm_zalloc(vm, sizeof(*vm->curr));
	vm->curr->vm = vm;
	vm->curr->top = vm->curr->stk;
	return 0;
}

static void vm_final_context(struct ymd_mach *vm) {
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

struct call_info *vm_nearcall(struct ymd_context *l) {
	struct call_info *i = l->info;
	if (!i->run)
		return NULL;
	while (i && i->run->is_c) {
		i = i->chain;
	}
	return i;
}

int vm_reached(struct ymd_mach *vm, const char *name) {
	struct hmap *lib = hmap_of(vm, vm_getg(vm, "__reached__"));
	struct variable *count = vm_mem(vm, lib, name);
	if (is_nil(count)) {
		vset_int(vm_def(vm, lib, name), 0);
		return 0;
	}
	++count->value.i;
	return count->value.i;
}

//------------------------------------------------------------------------
// Generic mapping functions:
// -----------------------------------------------------------------------
struct variable *vm_put(struct ymd_mach *vm,
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

static struct variable *vm_at(struct ymd_mach *vm, struct dyay *arr,
                              ymd_int_t i) {
	if (i < 0 || i >= arr->count)
		vm_die(vm, "Array out of range, index:%d, count:%d", i,
		       arr->count);
	return dyay_get(arr, i);
}

struct variable *vm_get(struct ymd_mach *vm, struct variable *var,
                        const struct variable *key) {
	if (is_nil(key))
		vm_die(vm, "No any key will be `nil`");
	switch (var->type) {
	case T_SKLS:
		return skls_get(skls_x(var), key);
	case T_HMAP:
		return hmap_get(hmap_x(var), key);
	case T_DYAY:
		return vm_at(vm, dyay_x(var), int_of(vm, key));
	default:
		vm_die(vm, "Variable can not be index");
		break;
	}
	assert(0); // Noreached!
	return NULL;
}

struct kstr *vm_strcat(struct ymd_mach *vm, const struct kstr *lhs, const struct kstr *rhs) {
	char *tmp = vm_zalloc(vm, lhs->len + rhs->len);
	struct kstr *x;
	memcpy(tmp, lhs->land, lhs->len);
	memcpy(tmp + lhs->len, rhs->land, rhs->len);
	x = vm_kstr(vm, tmp, lhs->len + rhs->len);
	vm_free(vm, tmp);
	return x;
}

struct variable *vm_def(struct ymd_mach *vm, void *o, const char *field) {
	struct variable k;
	switch (gcx(o)->type) {
	case T_HMAP:
		vset_kstr(&k, vm_kstr(vm, field, -1));
		return hmap_put(vm, o, &k);
	case T_SKLS:
		vset_kstr(&k, vm_kstr(vm, field, -1));
		return skls_put(vm, o, &k);
	default:
		assert(0);
		break;
	}
	return NULL;
}

struct variable *vm_mem(struct ymd_mach *vm, void *o, const char *field) {
	struct variable k;
	switch (gcx(o)->type) {
	case T_HMAP:
		vset_kstr(&k, vm_kstr(vm, field, -1));
		return hmap_get(o, &k);
	case T_SKLS:
		vset_kstr(&k, vm_kstr(vm, field, -1));
		return skls_get(o, &k);
	default:
		assert(0);
		break;
	}
	return NULL;
}

struct variable *vm_getg(struct ymd_mach *vm, const char *field) {
	struct variable k;
	vset_kstr(&k, vm_kstr(vm, field, -1));
	return hmap_get(vm->global, &k);
}

struct variable *vm_putg(struct ymd_mach *vm, const char *field) {
	struct variable k;
	vset_kstr(&k, vm_kstr(vm, field, -1));
	return hmap_put(vm, vm->global, &k);
}

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct kstr *vm_format(struct ymd_mach *vm, const char *fmt, ...) {
	va_list ap;
	int rv;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	rv = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return vm_kstr(vm, buf, -1);
}

//-----------------------------------------------------------------------------
// Mach:
// ----------------------------------------------------------------------------
struct ymd_mach *ymd_init() {
	struct ymd_mach *vm = calloc(1, sizeof(*vm));
	if (!vm)
		return NULL;
	// Basic memory functions:
	vm->zalloc  = default_zalloc;
	vm->free    = default_free;
	vm->die     = default_die;
	vm->tick    = 0;
	// Init gc:
	gc_init(vm, GC_THESHOLD);
	// Init global map:
	kpool_init(vm);
	vm->global = hmap_new(vm, -1);
	// Load symbols
	// `reached` variable: for all of loaded chunks
	vset_hmap(vm_putg(vm, "__reached__"), hmap_new(vm, -1));
	// Init context
	vm_init_context(vm);
	return vm;
}

void ymd_final(struct ymd_mach *vm) {
	vm_final_context(vm);
	if (vm->fn)
		mm_free(vm, vm->fn, (1 + vm->n_fn / FUNC_ALIGN) * FUNC_ALIGN,
				sizeof(*vm->fn));
	kpool_final(vm);
	gc_final(vm);
	assert(vm->gc.used == 0); // Must free all memory!
	free(vm);
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

void vm_die(struct ymd_mach *vm, const char *fmt, ...) {
	va_list ap;
	int rv;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	rv = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return vm->die(vm, buf);
}
