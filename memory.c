#include "memory.h"
#include "value.h"
#include "state.h"
#include "tostring.h"
#include <assert.h>
#include <stdio.h>

// GC Running:
static int gc_mark_var(struct variable *var);
static int gc_mark_hmap(struct hmap *o);
static int gc_mark_func(struct func *o);
static int gc_mark_dyay(struct dyay *o);
static int gc_mark_skls(struct skls *o);
static int gc_mark_mand(struct mand *o);
static void gc_adjust(struct ymd_mach *vm, size_t prev);

static inline int marked(struct variable *var, unsigned flag) {
	return (is_ref(var) ? (var->value.ref->marked & flag) != 0 : 1);
}

static inline int track(struct gc_struct *gc, struct gc_node *x) {
	x->next = gc->alloced;
	gc->alloced = x;
	return ++gc->n_alloced;
}

static int gc_mark_var(struct variable *var) {
	struct gc_node *o = var->value.ref;
	switch (var->type) {
	case T_KSTR:
		o->marked |= GC_BLACK_BIT0;
		break;
	case T_MAND:
		return gc_mark_mand(mand_f(o));
	case T_FUNC:
		return gc_mark_func(func_f(o));
	case T_DYAY:
		return gc_mark_dyay(dyay_f(o));
	case T_HMAP:
		return gc_mark_hmap(hmap_f(o));
	case T_SKLS:
		return gc_mark_skls(skls_f(o));
	}
	return 0;
}

static int gc_mark_func(struct func *o) {
	int i;
	struct chunk *core;
	o->marked |= GC_BLACK_BIT0;
	o->proto->marked |= GC_BLACK_BIT0;
	if (o->argv && (o->argv->marked & GC_BLACK_BIT0) == 0) {
		gc_mark_dyay(o->argv);
	}
	if (o->bind) {
		for (i = 0; i < o->n_bind; ++i) {
			if (!marked(o->bind + i, GC_BLACK_BIT0)) {
				gc_mark_var(o->bind + i);
			}
		}
	}
	if (o->is_c)
		return 0;
	core = o->u.core;
	core->file->marked |= GC_BLACK_BIT0;
	for (i = 0; i < core->klz; ++i)
		core->lz[i]->marked |= GC_BLACK_BIT0;
	for (i = 0; i < core->kkval; ++i) {
		if (!marked(core->kval + i, GC_BLACK_BIT0))
			gc_mark_var(core->kval + i);
	}
	return 0;
}

static int gc_mark_dyay(struct dyay *o) {
	int i;
	o->marked |= GC_BLACK_BIT0; // Mark self first
	for (i = 0; i < o->count; ++i) {
		if (!marked(o->elem + i, GC_BLACK_BIT0))
			gc_mark_var(o->elem + i);
	}
	return 0;
}

static int gc_mark_hmap(struct hmap *o) {
	struct kvi *initial = o->item,
			   *i = NULL,
			   *k = initial + (1 << o->shift);
	o->marked |= GC_BLACK_BIT0; // Mark self first
	for (i = initial; i != k; ++i) {
		if (!i->flag) continue;
		if (!marked(&i->k, GC_BLACK_BIT0))
			gc_mark_var(&i->k);
		if (!marked(&i->v, GC_BLACK_BIT0))
			gc_mark_var(&i->v);
	}
	return 0;
}

static int gc_mark_skls(struct skls *o) {
	struct sknd *i;
	o->marked |= GC_BLACK_BIT0; // Mark self first
	for (i = o->head->fwd[0]; i != NULL; i = i->fwd[0]) {
		if (!marked(&i->k, GC_BLACK_BIT0))
			gc_mark_var(&i->k);
		if (!marked(&i->v, GC_BLACK_BIT0))
			gc_mark_var(&i->v);
	}
	return 0;
}

static int gc_mark_mand(struct mand *o) {
	o->marked |= GC_BLACK_BIT0;
	if (!o->proto || gc_marked(o->proto, GC_BLACK_BIT0))
		return 1;
	switch (o->proto->type) {
	case T_HMAP:
		return gc_mark_hmap(hmap_f(o->proto));
	case T_SKLS:
		return gc_mark_skls(skls_f(o->proto));
	default:
		assert (0); // Noreached!
		break;
	}
	return 0;
}

static int gc_mark(struct ymd_mach *vm) {
	struct ymd_context *l = ioslate(vm);
	// Mark all reached variable from global
	gc_mark_hmap(vm->global);
	if (l->info) { // Mark all local variable
		int n = func_nlocal(l->info->run);
		struct variable *i, *k = l->info->loc + n;
		for (i = l->loc; i != k; ++i)
			if (!marked(i, GC_BLACK_BIT0))
				gc_mark_var(i);

	}
	if (l->info) { // Mark all function in stack
		struct call_info *i = l->info;
		while (i) {
			if ((i->run->marked & GC_BLACK_BIT0) == 0)
				gc_mark_func(i->run);
			i = i->chain;
		}
	}
	if (l->stk != l->top) { // Mark all stack variable
		struct variable *i, *k = l->top;
		for (i = l->stk; i != k; ++i)
			if (!marked(i, GC_BLACK_BIT0))
				gc_mark_var(i);
	}
	return 0;
}

static void gc_hook(struct ymd_mach *vm, struct gc_node *i) {
	(void)vm;
	(void)i;
}

static int gc_kpool_sweep(struct ymd_mach *vm) {
	int rv = 0;
	struct kpool *kt = &vm->kpool;
	struct gc_node **i, **k = kt->slot + (1 << kt->shift);
	for (i = kt->slot; i != k; ++i) {
		if (*i) {
			struct gc_node dummy;
			struct gc_node *x = *i, *p = &dummy;
			memset(&dummy, 0, sizeof(dummy));
			dummy.next = x;
			while (x) {
				if ((x->marked & (GC_BLACK_BIT0 | GC_GRAY_BIT0)) == 0) {
					p->next = x->next;
					gc_hook(vm, x);
					gc_del(vm, x);
					kt->used--;
					x = p->next;
					rv++;
				} else {
					if (x->marked & GC_GRAY_BIT0)
						++vm->gc.grab;
					x->marked &= ~GC_BLACK_BIT0;
					p = x;
					x = x->next;
				}
			}
			*i = dummy.next;
		}
	}
	return rv;
}

static size_t gc_sweep(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	size_t old = gc->used;
	struct gc_node dummy, *i = gc->alloced, *p = &dummy;
	gc_kpool_sweep(vm);
	memset(&dummy, 0, sizeof(dummy));
	dummy.next = i;
	while (i) {
		if ((i->marked & (GC_BLACK_BIT0 | GC_GRAY_BIT0)) == 0) {
			p->next = i->next;
			gc_hook(vm, i);
			gc_del(vm, i);
			i = p->next;
		} else {
			if (i->marked & GC_GRAY_BIT0)
				++gc->grab;
			i->marked &= ~GC_BLACK_BIT0;
			p = i;
			i = i->next;
		}
	}
	gc->alloced = dummy.next;
	return old - gc->used;
}

static size_t gc_record(struct ymd_mach *vm, size_t inc, int neg) {
	size_t rv = 0;
	struct gc_struct *gc = &vm->gc;
	if (neg)
		gc->used -= inc;
	else
		gc->used += inc;
	if (neg)
		return 0;
	if (gc->used > gc->threshold) {
		if (!gc->pause)
			gc_mark(vm);
		if (!gc->pause)
			gc_adjust(vm, gc_sweep(vm));
	}
	return rv;
}

void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *x = vm_zalloc(vm, size);
	assert(type < (1 << 4));
	x->type = type;
	x->marked = GC_GRAY_BIT0;
	gc_record(vm, size, 0);
	track(gc, x);
	return x;
}

int gc_init(struct ymd_mach *vm, int k) {
	struct gc_struct *gc = &vm->gc;
	gc->alloced   = NULL;
	gc->n_alloced = 0;
	gc->threshold = k;
	gc->pause     = 0;
	gc->logf      = NULL;
	return 0;
}

int gc_active(struct ymd_mach *vm, int act) {
	struct gc_struct *gc = &vm->gc;
	int old = gc->pause;
	switch (act) {
	case GC_PAUSE:
		++gc->pause;
		break;
	case GC_IDLE:
		--gc->pause;
		break;
	case GC_MARK:
		if (!gc->pause) gc_mark(vm);
		break;
	case GC_SWEEP:
		if (!gc->pause) gc_adjust(vm, gc_sweep(vm));
		break;
	default:
		assert(0);
		break;
	}
	assert(gc->pause >= 0);
	return old;
}

void gc_del(struct ymd_mach *vm, void *p) {
	struct gc_node *o = p;
	size_t chunk = 0;
	switch (o->type) {
	case T_KSTR:
		chunk = sizeof(struct kstr);
		chunk += kstr_f(o)->len;
		break;
	case T_FUNC:
		func_final(vm, func_f(o));
		chunk = sizeof(struct func);
		break;
	case T_DYAY:
		dyay_final(vm, dyay_f(o));
		chunk = sizeof(struct dyay);
		break;
	case T_HMAP:
		hmap_final(vm, hmap_f(o));
		chunk = sizeof(struct hmap);
		break;
	case T_SKLS:
		skls_final(vm, skls_f(o));
		chunk = sizeof(struct skls);
		break;
	case T_MAND:
		mand_final(vm, mand_f(o));
		chunk = sizeof(struct mand);
		chunk += mand_f(o)->len;
		break;
	default:
		assert(0);
		return;
	}
	mm_free(vm, o, 1, chunk);
	--(vm->gc.n_alloced);
}

void gc_final(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *i = gc->alloced, *p = i;
	while (i) {
		p = i;
		i = i->next;
		gc_del(vm, p);
	}
}

static void gc_adjust(struct ymd_mach *vm, size_t prev) {
	struct gc_struct *gc = &vm->gc;
	float rate = (float)prev / (float)gc->used;
	if (rate < 0.1f) // collected < 1% 
		gc->threshold <<= 1;
	else if (rate >= 0.6f) // collected > 6%
		gc->threshold >>= 1;
	if (prev > 0 && gc->logf)
		fprintf(gc->logf, "Full GC: %zd\t%zd\t%zd\t%02f\t%lld\t%d\n",
		        prev, gc->used + prev, gc->threshold, rate,
				vm->tick - gc->last, gc->grab);
	gc->last = vm->tick;
	gc->grab = 0;
}

void *mm_zalloc(struct ymd_mach *vm, int n, size_t chunk) {
	assert(n > 0);
	assert(chunk > 0);
	gc_record(vm, n * chunk, 0);
	return vm_zalloc(vm, n * chunk);
}

void *mm_realloc(struct ymd_mach *vm, void *raw, int old, int n,
                 size_t chunk) {
	assert(n > 0);
	assert(chunk > 0);
	assert(n > old);
	gc_record(vm, (n - old) * chunk, 0);
	return vm_realloc(vm, raw, n * chunk);
}

void mm_free(struct ymd_mach *vm, void *raw, int n, size_t chunk) {
	assert(n > 0);
	assert(chunk > 0);
	gc_record(vm, n * chunk, 1);
	vm_free(vm, raw);
}

void *mm_need(struct ymd_mach *vm, void *raw, int n, int align,
              size_t chunk) {
	char *rv;
	if (n % align)
		return raw;
	rv = vm_realloc(vm, raw, chunk * (n + align));
	memset(rv + chunk * n, 0, chunk * align);
	gc_record(vm, chunk * align, 0);
	return rv;
}

void *mm_shrink(struct ymd_mach *vm, void *raw, int n, int align,
                size_t chunk) {
	void *bak;
	assert(raw != NULL);
	assert(n > 0);
	if (n % align == 0)
		return raw;
	bak = vm_zalloc(vm, chunk * n);
	memcpy(bak, raw, chunk * n);
	vm_free(vm, raw);
	gc_record(vm, chunk * (align - (n % align)), 1);
	return bak;
}

void mm_drop(struct ymd_mach *vm, void *p, size_t size) {
	int *ref = p;
	--(*ref);
	if (*ref == 0)
		mm_free(vm, p, 1, size);
}
