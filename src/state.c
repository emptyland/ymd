#include "core.h"
#include "print.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define MAX_MSG_LEN 1024

static void vm_backtrace(struct ymd_context *l, int max);
static int vm_init_context(struct ymd_mach *vm);
static void vm_final_context(struct ymd_mach *vm);

static void *default_zalloc(struct ymd_mach *vm, void *p, size_t size) {
	void *chunk = !p ? calloc(size, 1) : realloc(p, size);
	if (!chunk) {
		if (p)
			free(p);
		ymd_panic(ioslate(vm), "System: Not enough memory");
	}
	assert(chunk != NULL);
	return chunk;
}

static void default_free(struct ymd_mach *vm, void *chunk) {
	if (!chunk)
		ymd_panic(ioslate(vm), "Fatal: NULL free");
	free(chunk);
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
		if (i->run->is_c)
			ymd_fprintf(stderr, " ${[green]>}$ %s\n", i->run->proto->land);
		else
			ymd_fprintf(stderr, " ${[green]>}$ %s:%d %s\n",
			        i->run->u.core->file->land,
					i->run->u.core->line[i->pc - 1],
					i->run->proto->land);
		i = i->chain;
	}
	if (i != NULL)
		ymd_fprintf(stderr, " ${[green]>}$ ... more calls ...\n");
}

struct call_info *vm_nearcall(struct ymd_context *l) {
	struct call_info *i = l->info;
	if (!i || !i->run)
		return NULL;
	while (i && i->run->is_c) {
		i = i->chain;
	}
	return i;
}

int vm_reached(struct ymd_mach *vm, const char *name) {
	struct hmap *lib = hmap_of(ioslate(vm), vm_getg(vm, "__reached__"));
	struct variable *count = vm_mem(vm, lib, name);
	if (is_nil(count)) {
		setv_int(vm_def(vm, lib, name), 0);
		return 0;
	}
	++count->u.i;
	return count->u.i;
}

//------------------------------------------------------------------------
// Generic mapping functions:
// -----------------------------------------------------------------------
struct variable *vm_put(struct ymd_mach *vm,
                        struct variable *var,
                        const struct variable *key) {
	struct ymd_context *l = ioslate(vm);
	if (is_nil(key))
		ymd_panic(l, "No any key will be `nil`");
	switch (TYPEV(var)) {
	case T_SKLS:
		return skls_put(vm, skls_x(var), key);
	case T_HMAP:
		return hmap_put(vm, hmap_x(var), key);
	case T_DYAY:
		return dyay_get(dyay_x(var), int_of(l, key));
	case T_MAND:
		if (!mand_x(var)->proto)
			ymd_panic(l, "Management memory has no metatable yet");
		return mand_put(vm, mand_x(var), key);
	default:
		ymd_panic(l, "Variable can not be put");
		break;
	}
	return NULL;
}

static struct variable *vm_at(struct ymd_mach *vm, struct dyay *arr,
                              ymd_int_t i) {
	if (i < 0 || i >= arr->count)
		ymd_panic(ioslate(vm), "Array out of range, index:%d, count:%d", i,
		          arr->count);
	return dyay_get(arr, i);
}

struct variable *vm_get(struct ymd_mach *vm, struct variable *var,
                        const struct variable *key) {
	struct ymd_context *l = ioslate(vm);
	if (is_nil(key))
		ymd_panic(l, "No any key will be `nil`");
	switch (TYPEV(var)) {
	case T_SKLS:
		return skls_get(skls_x(var), key);
	case T_HMAP:
		return hmap_get(hmap_x(var), key);
	case T_DYAY:
		return vm_at(vm, dyay_x(var), int_of(l, key));
	case T_MAND:
		if (!mand_x(var)->proto)
			ymd_panic(l, "Management memory has no metatable yet");
		return mand_get(mand_x(var), key);
	default:
		ymd_panic(l, "Variable can not be index");
		break;
	}
	assert(!"No reached.");
	return NULL;
}

struct kstr *vm_strcat(struct ymd_mach *vm, const struct kstr *lhs,
                       const struct kstr *rhs) {
	char *tmp = vm_zalloc(vm, lhs->len + rhs->len);
	struct kstr *x;
	memcpy(tmp, lhs->land, lhs->len);
	memcpy(tmp + lhs->len, rhs->land, rhs->len);
	x = kstr_fetch(vm, tmp, lhs->len + rhs->len);
	vm_free(vm, tmp);
	return x;
}

struct variable *vm_def(struct ymd_mach *vm, void *o, const char *field) {
	struct variable k, *v;
	switch (gcx(o)->type) {
	case T_HMAP:
		setv_kstr(&k, kstr_fetch(vm, field, -1));
		v = hmap_put(vm, o, &k);
		break;
	case T_SKLS:
		setv_kstr(&k, kstr_fetch(vm, field, -1));
		v = skls_put(vm, o, &k);
		break;
	default:
		assert(!"No reached.");
		break;
	}
	return v;
}

struct variable *vm_mem(struct ymd_mach *vm, void *o, const char *field) {
	struct variable k, *v = NULL;
	switch (gcx(o)->type) {
	case T_HMAP:
		setv_kstr(&k, kstr_fetch(vm, field, -1));
		v = hmap_get(o, &k);
		break;
	case T_SKLS:
		setv_kstr(&k, kstr_fetch(vm, field, -1));
		v = skls_get(o, &k);
		break;
	default:
		assert(!"No reached.");
		break;
	}
	return v;
}

struct variable *vm_getg(struct ymd_mach *vm, const char *field) {
	struct variable k, *v;
	setv_kstr(&k, kstr_fetch(vm, field, -1));
	v = hmap_get(vm->global, &k);
	return v;
}

struct variable *vm_putg(struct ymd_mach *vm, const char *field) {
	struct variable k, *v;
	setv_kstr(&k, kstr_fetch(vm, field, -1));
	v = hmap_put(vm, vm->global, &k);
	return v;
}

int vm_remove(struct ymd_mach *vm, struct variable *var,
              const struct variable *key) {
	struct ymd_context *l = ioslate(vm);
	switch (TYPEV(var)) {
	case T_DYAY:
		return dyay_remove(vm, dyay_x(var), int_of(l, key));
	case T_HMAP:
		return hmap_remove(vm, hmap_x(var), key);
	case T_SKLS:
		return skls_remove(vm, skls_x(var), key);
	}
	ymd_panic(l, "Variable can not be remove");
	return 0;
}

//-----------------------------------------------------------------------------
// Function table:
// ----------------------------------------------------------------------------
struct kstr *vm_format(struct ymd_mach *vm, const char *fmt, ...) {
	va_list ap;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return kstr_fetch(vm, buf, -1);
}

void ymd_format(struct ymd_context *l, const char *fmt, ... ) {
	va_list ap;
	char buf[MAX_MSG_LEN];
	struct kstr *o;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	o = kstr_fetch(l->vm, buf, -1);
	setv_kstr(ymd_push(l), o);
}

struct variable *ymd_upval(struct ymd_context *l, int i) {
	struct func *fn = ymd_called(l);
	int k = fn->is_c ? fn->n_upval : fn->u.core->kuz;
#ifndef NDEBUG
	if (!fn->is_c) {
		assert(fn->n_upval == fn->u.core->kuz 
				&& "Function's n_upval and u.core->kuz not synchronous!");
	}
#endif
	if (i < 0 || i >= k)
		ymd_panic(l, "Upval index out of range, %d vs. [%d, %d)",
				i, 0, k);
	return fn->upval + i;
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
	vm->tick    = 0;
	// Init gc:
	gc_init(vm, GC_THESHOLD);
	// Init global map:
	kpool_init(vm);
	vm->global = hmap_new(vm, -1);
	vm->global->marked = GC_FIXED; // global is a fixed object.
	// Init context
	vm_init_context(vm);
	// Load symbols
	// `__reached__' variable: for all of loaded chunks
	ymd_hmap(ioslate(vm), 1);
	ymd_putg(ioslate(vm), "__reached__");
	// `__g__' is global map
	setv_hmap(ymd_push(ioslate(vm)), vm->global);
	ymd_putg(ioslate(vm), "__g__");
	return vm;
}

void ymd_final(struct ymd_mach *vm) {
	gc_final(vm);
	kpool_final(vm);
	vm_final_context(vm);
	assert (vm->gc.used == 0); // Must free all memory!
	assert (vm->gc.n_alloced == 0); // Allocated object must be zero.
	free(vm);
}

static void do_panic(struct ymd_context *l, const char *msg) {
	struct call_info *i = vm_nearcall(l);
	if (!msg || !*msg)
		msg = "Unknown!";
	if (i)
		ymd_fprintf(stderr, "%s:%d: ${[red]panic:}$ %s\n",
				i->run->u.core->file->land,
				i->run->u.core->line[i->pc - 1], msg);
	else
		ymd_fprintf(stderr, "[none] ${[red]panic:}$ %s\n", msg);
	fprintf(stderr, "-- Back trace:\n");
	vm_backtrace(l, 6);
	assert(l->jpt);
	l->jpt->panic = 1;
	longjmp(l->jpt->core, 1);
}

void ymd_panic(struct ymd_context *l, const char *fmt, ...) {
	va_list ap;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return do_panic(l, buf);
}

void ymd_raise(struct ymd_context *l) {
	assert (l->jpt);
	// NOTE: Call chain back and cleanup argv!
	if (ymd_called(l)->argv)
		dyay_final(l->vm, ymd_called(l)->argv);
	l->info = l->info->chain;
	l->jpt->panic = 0;
	longjmp(l->jpt->core, l->jpt->level);
}

int ymd_error(struct ymd_context *l, const char *msg) {
	struct call_info *ci = vm_nearcall(l);
	if (msg)
		ymd_kstr(l, msg, -1);
	if (ci) {
		struct func *run = ci->run;
		ymd_format(l, "%s:%d %s", run->u.core->file->land,
				   run->u.core->line[ci->pc - 1],
				   run->proto->land);
	} else {
		ymd_nil(l);
	}
	ci = l->info;
	ymd_dyay(l, 8);
	while (ci) {
		struct func *run = ci->run;
		if (run->is_c)
			ymd_format(l, "%s", run->proto->land);
		else
			ymd_format(l, "%s:%d %s", run->u.core->file->land,
			           run->u.core->line[ci->pc - 1],
					   run->proto->land);
		ymd_add(l);
		ci = ci->chain;
	}
	return msg ? 3 : 2;
}

