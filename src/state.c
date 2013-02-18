#include "core.h"
#include "tostring.h"
#include "zstream.h"
#include "print.h"
#include "third_party/pcre/pcre.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define MAX_MSG_LEN 1024

#define K_DECL(v) \
	v(GLOBAL, "__G__") \
	v(LOADED, "__loaded__") \
	v(STAR_ALL, "*all") \
	v(STAR_LINE, "*line") \
	v(STAR_NOSUB, "*nosub") \
	v(STAR_SUB, "*sub") \
	v(RESUME, "resume") \
	v(STEP, "step") \
	v(USED, "used") \
	v(THRESHOLD, "threshold") \
	v(STATE, "state") \
	v(PAUSE, "pause") \
	v(SWEEPSTEP, "sweepstep") \
	v(PROPAGATE, "propagate") \
	v(SWEEPSTRING, "sweepstring") \
	v(SWEEP, "sweep") \
	v(FINALIZE, "finalize")

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
	vm->curr->kstk = YMD_INIT_STACK;
	vm->curr->stk = vm_zalloc(vm, vm->curr->kstk * sizeof(*vm->curr->stk));
	vm->curr->top = vm->curr->stk;
	return 0;
}

static void vm_final_context(struct ymd_mach *vm) {
	vm_free(vm, vm->curr->stk);
	vm_free(vm, vm->curr);
	vm->curr = NULL;
}

// Dump backtrace information
static void vm_backtrace(struct ymd_context *l, int max) {
	struct call_info *i = l->info;
	assert(max >= 0);
	while (i && max--) {
		if (i->run->is_c)
			ymd_fprintf(stderr, " ${[green]>}$ %s\n",
					func_proto(l->vm, i->run)->land);
		else
			ymd_fprintf(stderr, " ${[green]>}$ %s:%d %s\n",
			        i->run->u.core->file->land,
					i->run->u.core->line[i->pc - 1],
					func_proto(l->vm, i->run)->land);
		i = i->chain;
	}
	if (i != NULL)
		ymd_fprintf(stderr, " ${[green]>}$ ... more calls ...\n");
}

// Dump stack information
static void vm_stack(struct ymd_context *l, int max) {
	const struct variable *i = ymd_top(l, 0);
	while (i >= l->stk && max--) {
		struct zostream os = ZOS_INIT;
		ymd_fprintf(stderr, " ${[green][%ld]}$ %s\n", ymd_top(l, 0) - i,
				tostring(&os, i));
		--i;
		zos_final(&os);
	}
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

int vm_loaded(struct ymd_mach *vm, const char *name) {
	struct hmap *lib = hmap_of(ioslate(vm), vm_getg(vm, "__loaded__"));
	struct variable *count = vm_mem(vm, lib, name);
	if (is_nil(count)) {
		setv_int(vm_def(vm, lib, name), 0);
		return 0;
	}
	++count->u.i;
	return count->u.i;
}

int vm_bool(const struct variable *lhs) {
	switch (ymd_type(lhs)) {
	case T_NIL:
		return 0;
	case T_BOOL:
		return lhs->u.i;
	default:
		break;
	}
	return 1;
}

void vm_pcre_lazy(struct ymd_mach *vm) {
	if (vm->pcre_js)
		return;
	vm->pcre_js = pcre_jit_stack_alloc(YMD_JS_START, YMD_JS_MAX);
	assert (vm->pcre_js);
}
//------------------------------------------------------------------------
// Generic mapping functions:
// -----------------------------------------------------------------------
struct variable *vm_put(struct ymd_mach *vm,
		struct variable *var,
		const struct variable *key) {
	struct ymd_context *l = ioslate(vm);
	if (is_nil(key))
		ymd_panic(l, "No any key will be `nil'");
	switch (ymd_type(var)) {
	case T_SKLS:
		return skls_put(vm, skls_x(var), key);
	case T_HMAP:
		return hmap_put(vm, hmap_x(var), key);
	case T_DYAY:
		return dyay_get(dyay_x(var), int4of(l, key));
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
	switch (ymd_type(var)) {
	case T_SKLS:
		return skls_get(vm, skls_x(var), key);
	case T_HMAP:
		return hmap_get(hmap_x(var), key);
	case T_DYAY:
		return vm_at(vm, dyay_x(var), int_of(l, key));
	case T_MAND:
		if (!mand_x(var)->proto)
			ymd_panic(l, "Management memory has no metatable yet");
		return mand_get(vm, mand_x(var), key);
	default:
		ymd_panic(l, "Variable can not be index");
		break;
	}
	assert(!"No reached.");
	return NULL;
}

int vm_remove(struct ymd_mach *vm, struct variable *var,
              const struct variable *key) {
	struct ymd_context *l = ioslate(vm);
	switch (ymd_type(var)) {
	case T_DYAY:
		return dyay_remove(vm, dyay_x(var), int4of(l, key));
	case T_HMAP:
		return hmap_remove(vm, hmap_x(var), key);
	case T_SKLS:
		return skls_remove(vm, skls_x(var), key);
	case T_MAND:
		return mand_remove(vm, mand_x(var), key);
	}
	ymd_panic(l, "Variable can not be remove");
	return 0;
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
		return NULL;
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
		v = skls_get(vm, o, &k);
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
	// `__loaded__' variable: for all of loaded chunks
	ymd_hmap(ioslate(vm), 1);
	ymd_putg(ioslate(vm), "__loaded__");
	// `__g__' is global map
	setv_hmap(ymd_push(ioslate(vm)), vm->global);
	ymd_putg(ioslate(vm), "__g__");
	return vm;
}

void ymd_final(struct ymd_mach *vm) {
	gc_final(vm);
	kpool_final(vm);
	vm_final_context(vm);
	if (vm->pcre_js)
		pcre_jit_stack_free(vm->pcre_js);
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
	fprintf(stderr, "-- Stack:\n");
	vm_stack(l, 6);
	fprintf(stderr, "-- Back trace:\n");
	vm_backtrace(l, 6);
	if (!l->jpt) {
		fprintf(stderr, "-- Not one catch it\n");
		return;
	}
	l->vm->fatal = 1; // Set fatal flag.
	l->jpt->panic = 1;
	longjmp(l->jpt->core, 1);
}

void ymd_panic(struct ymd_context *l, const char *fmt, ...) {
	va_list ap;
	char buf[MAX_MSG_LEN];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	do_panic(l, buf);
}

// Stack Adjust:
// print (1, 2, 3)
// print [4] 
// 1 [3]     <- curr->u.lea
// 2 [2]
// 3 [1]
// (error info) [0]
void ymd_raise(struct ymd_context *l) {
	int i;
	struct call_info *curr = l->info;
	int balance = curr->argc + (curr->adjust ? 0 : 1);
	struct variable info[3];
	assert (l->jpt);
	assert (curr->run->is_c);
	// FIXME: Save the error information
	for (i = 0; i < 3; ++i)
		info[3 - i - 1] = *ymd_top(l, i);
	ymd_pop(l, 3 + balance);
	for (i = 0; i < 3; ++i)
		*ymd_push(l) = info[i];
	// Jump to near point
	l->info = curr->chain;
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
				   func_proto(l->vm, run)->land);
	} else {
		ymd_nil(l);
	}
	ci = l->info;
	ymd_dyay(l, 8);
	while (ci) {
		struct func *run = ci->run;
		if (run->is_c)
			ymd_format(l, "%s", func_proto(l->vm, run)->land);
		else
			ymd_format(l, "%s:%d %s", run->u.core->file->land,
			           run->u.core->line[ci->pc - 1],
					   func_proto(l->vm, run)->land);
		ymd_add(l);
		ci = ci->chain;
	}
	return msg ? 3 : 2;
}

struct variable *ymd_push(struct ymd_context *l) {
	if (l->top >= l->stk + YMD_MAX_STACK)
		ymd_panic(l, "Stack overflow!");
	if (l->top >= l->stk + l->kstk) {
		l->stk = vm_realloc(l->vm, l->stk,
				(l->kstk << 1) * sizeof(*l->stk));
		l->top = l->stk + l->kstk;
		memset(l->top, 0, l->kstk * sizeof(*l->top));
		l->kstk <<= 1;
	}
	++l->top;
	return l->top - 1;
}

static void stack_shrink(struct ymd_context *l, size_t offset) {
	struct variable *bak = NULL;
	size_t shrink = l->kstk;
	while (shrink > YMD_MAX(YMD_INIT_STACK, offset))
		shrink >>= 1;
	// Shrink stack to 1/2
	bak = vm_zalloc(l->vm, shrink * sizeof(*l->stk));
	memcpy(bak, l->stk, offset * sizeof(*l->stk));
	vm_free(l->vm, l->stk);
	l->stk = bak;
	l->top = l->stk + offset;
	l->kstk = shrink;
}

void ymd_pop(struct ymd_context *l, int n) {
	size_t offset;
	if (n > 0 && l->top == l->stk)
		ymd_panic(l, "Stack empty!");
	if (n > l->top - l->stk)
		ymd_panic(l, "Bad pop!");
	l->top -= n;
	offset = l->top - l->stk;
	// offset less than 1/4
	if (l->kstk > YMD_INIT_STACK && offset < (l->kstk >> 2))
		stack_shrink(l, offset);
	else
		memset(l->top, 0, sizeof(*l->top) * n);
}

struct variable *ymd_top(struct ymd_context *l, int i) {
	if (l->top == l->stk)
		ymd_panic(l, "Stack empty!");
	if (i < 0 && 1 - i >= l->top - l->stk)
		ymd_panic(l, "Stack out of range [%d]", i);
	if (i > 0 &&     i >= l->top - l->stk)
		ymd_panic(l, "Stack out of range [%d]", i);
	return i < 0 ? l->stk + 1 - i : l->top - 1 - i;
}
