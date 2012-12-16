#include "memory.h"
#include "value.h"
#include "state.h"
#include "tostring.h"
#include <assert.h>
#include <stdio.h>

//-----------------------------------------------------------------------------
// GC functions:
//-----------------------------------------------------------------------------

#define gc_marko(o) \
	if (gc_whiteo(o)) \
		gc_white2gray(gcx(o))

#define gc_markv(v) \
	if ((v)->tt == T_REF && \
			gc_whiteo((v)->u.ref)) \
		gc_mark_obj((v)->u.ref)

#define gc_travelo(o) \
	if (!mm_busy(o)) \
		gc_travel_obj(gcx(o))

#define gc_travelv(v) \
	if ((v)->tt == T_REF && \
			!mm_busy((v)->u.ref)) \
		gc_travel_obj((v)->u.ref)


static void gc_adjust(struct ymd_mach *vm, size_t prev);
static void gc_mark_root(struct ymd_mach *vm);
static void gc_propagate(struct ymd_mach *vm);
static void gc_atomic(struct ymd_mach *vm);
static void gc_sweep_kpool(struct ymd_mach *vm);
static void gc_final_kpool(struct ymd_mach *vm);
static void gc_sweep(struct ymd_mach *vm);
static void gc_final_sweep(struct ymd_mach *vm);
static int gc_mark_global(struct ymd_mach *vm);
static int gc_mark_context(struct ymd_mach *vm);
static void gc_move2gray(struct ymd_mach *vm);
static void gc_mark_obj(struct gc_node *o);
static void  gc_travel_obj(struct gc_node *o);
static int gc_travel_func(struct func *o);

static inline void gc_white2gray(struct gc_node *o) {
	assert (gc_whiteo(o) && "Only white object can become gray.");
	assert (!gc_fixedo(o) && "Don't mark fixed object.");
	o->marked = (o->marked & ~GC_MASK) | GC_GRAY;
}

static inline void gc_white2black(struct gc_node *o) {
	assert (gc_whiteo(o) && "Only white object can become black.");
	assert (!gc_fixedo(o) && "Don't mark fixed object.");
	o->marked = (o->marked & ~GC_MASK) | GC_BLACK;
}

static inline void gc_gray2black(struct gc_node *o) {
	assert (!gc_fixedo(o) && "Don't mark fixed object.");
	o->marked = (o->marked & ~GC_MASK) | GC_BLACK;
}

static inline void gc_black2white(struct gc_node *o, int white) {
	assert (gc_blacko(o) && "Only black object can become white.");
	assert (!gc_fixedo(o) && "Don't mark fixed object.");
	assert (white == GC_WHITE0 || white == GC_WHITE1);
	o->marked = (o->marked & ~GC_MASK) | white;
}

// Can we sweep a object?
static inline int gc_should_sweep(const struct gc_struct *gc,
		const struct gc_node *o) {
	assert (!mm_busy(o));
	return !gc_fixedo(o) && o->marked == gc_otherwhite(gc->white);
}

// Get value between gc->point and gc->used: 
static inline int gc_delta(const struct gc_struct *gc) {
	return gc->point > gc->used ? gc->point - gc->used :
		gc->used - gc->point;
}

static void gc_hook(struct ymd_mach *vm, struct gc_node *o) {
	(void)vm;
	(void)o;
	// TODO: For debugging.
}

static size_t gc_record(struct ymd_mach *vm, size_t inc, int neg) {
	struct gc_struct *gc = &vm->gc;
	if (neg)
		gc->used -= inc;
	else
		gc->used += inc;
	return 0;
}

void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *x = vm_zalloc(vm, size);
	assert(type < (1 << 4));
	x->type = type;
	x->marked = gc->white; // Initial mark is current white.
	gc_record(vm, size, 0);
	// Link it in alloced list.
	x->next = gc->alloced;
	gc->alloced = x;
	++gc->n_alloced;
	return x;
}

int gc_init(struct ymd_mach *vm, int k) {
	struct gc_struct *gc = &vm->gc;
	gc->threshold = k;
	gc->state = GC_PAUSE;
	gc->white = GC_WHITE0;
	return 0;
}

int gc_active(struct ymd_mach *vm, int off) {
	struct gc_struct *gc = &vm->gc;
	int old = gc->pause;
	if (off > 0)
		++gc->pause;
	else if (off < 0)
		--gc->pause;
	assert(gc->pause >= 0);
	return old;
}

int gc_step(struct ymd_mach *vm) { // Run gc in one step.
	struct gc_struct *gc = &vm->gc;
	if (gc->pause)
		return -1;
	switch (gc->state) {
	case GC_PAUSE:
		gc_mark_root(vm);
		break;
	case GC_PROPAGATE:
		if (gc->gray)
			gc_propagate(vm);
		else
			gc_atomic(vm);
		break;
	case GC_SWEEPSTRING:
		if (gc->sweep_kpool < (1 << vm->kpool.shift))
			gc_sweep_kpool(vm);
		else
			gc_final_kpool(vm);
		break;
	case GC_SWEEP:
		if (gc->sweep)
			gc_sweep(vm);
		else
			gc_final_sweep(vm);
		break;
	case GC_FINALIZE:
		gc_adjust(vm, gc_delta(gc));
		gc->point = 0;
		gc->state = GC_PAUSE;
		break;
	default:
		assert (!"No reached.");
		break;
	}
	return 0;
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
		assert(!"No reached.");
		return;
	}
	mm_free(vm, o, 1, chunk);
	--(vm->gc.n_alloced);
}

void gc_final(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *i, *p;
	// Run gc in last one.
	gc->pause = 0;
	while (gc->state != GC_FINALIZE)
		gc_step(vm);
	// Delete all allocated objects.
	i = gc->alloced;
	p = i;
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
				vm->tick - gc->last, gc->fixed);
	gc->last = vm->tick;
	gc->fixed = 0;
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

// Mark all reached object white to gray color.
// This process is ATOMIC.
static void gc_mark_root(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	// Mark all reached variable from global, no deeped.
	gc_mark_global(vm);
	gc_mark_context(vm);
	gc_move2gray(vm);
	gc->fixed = 0;
	// Mark finialize, change gc state to next step.
	gc->state = GC_PROPAGATE;
}

// Incrmential change gray to black.
static void gc_propagate(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node *curr = gc->gray;
	assert (gc->gray && "Gray list already empty.");
	// Remark one object in one time.
	gc_travel_obj(curr);
	// Remove current node from gray list to gray_again list.
	gc->gray = curr->next;
	curr->next = gc->gray_again;
	gc->gray_again = curr;
}

// Finilize propagate.
static void gc_atomic(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	// Remark again:
	gc->gray = gc->gray_again;
	gc->gray_again = NULL;
	gc_mark_global(vm);
	gc_mark_context(vm);
	gc_move2gray(vm);
	while (gc->gray) gc_propagate(vm);
	// Change gc's white flag:
	gc->white = gc_otherwhite(vm->gc.white);
	// Check Point, save used byte in GC beginning.
	gc->point = gc->used;
	// Sweep string step number:
	gc->sweep_step  = vm->kpool.shift;
	gc->sweep_kpool = 0;
	// -> sweep string
	gc->state = GC_SWEEPSTRING;
}

// Sweep constant string pool(hash table)
static void gc_sweep_kpool(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct kpool *kt = &vm->kpool;
	int step = gc->sweep_step;
	struct gc_node **i = kt->slot + gc->sweep_kpool,
				   **k = kt->slot + (1 << kt->shift);
	for (; i != k; ++i) {
		struct gc_node dummy = {*i, 0, 0, 0}, *p = &dummy, *x = *i;
		// Find fisrt valid slot
		if (!x)
			continue; 
		while (x) {
			// Sweep a list in one time.
			assert (!gc_grayo(x) &&
					"No pooled string can be gray.");
			if (gc_should_sweep(gc, x)) {
				p->next = x->next;
				gc_hook(vm, x);
				gc_del(vm, x);
				--kt->used;
				x = p->next;
			} else {
				if (gc_fixedo(x))
					++vm->gc.fixed;
				if (gc_blacko(x))
					gc_black2white(x, gc->white);
				p = x;
				x = x->next;
			}
		}
		*i = dummy.next;
		if (!step--) break;
	}
	// Save current slot's index.
	gc->sweep_kpool = (int)(++i - kt->slot);
}

static void gc_final_kpool(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;

	// Sweep objects step number
	// We hope sweep 1k by one step:
	gc->sweep_step = gc_delta(gc) / (sizeof(struct variable) * 100);
	gc->sweep_step = gc->sweep_step <= 0 ? 1 : gc->sweep_step;
	// Move alloced list to sweep list for sweeping.
	gc->sweep = gc->alloced;
	gc->alloced = NULL;
	// -> sweep objects
	gc->state = GC_SWEEP;
}

// Incrmential sweep non-string objects
static void gc_sweep(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	int step = gc->sweep_step;
	// Sweep some object by `sweep_setp' in one time
	while (step && gc->sweep) {
		struct gc_node *x = gc->sweep;
		assert (!gc_grayo(x) &&
				"No sweeping object can be gray.");
		gc->sweep = gc->sweep->next;
		if (gc_should_sweep(gc, x)) {
			gc_hook(vm, x);
			gc_del(vm, x);
			--step;
		} else {
			x->next = gc->alloced;
			if (gc_fixedo(x))
				++vm->gc.fixed;
			if (gc_blacko(x))
				gc_black2white(x, gc->white);
			gc->alloced = x;
		}
	}
}

static void gc_final_sweep(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	// Move gray_again list to alloced list
	while (gc->gray_again) {
		struct gc_node *x = gc->gray_again;
		gc->gray_again = gc->gray_again->next;
		x->next = gc->alloced;
		gc_black2white(x, gc->white);
		gc->alloced = x;
	}
	assert (!gc->gray_again);
	gc->state = GC_FINALIZE;
}

static int gc_mark_global(struct ymd_mach *vm) {
	struct kvi *initial = vm->global->item,
			   *i = NULL,
			   *k = initial + (1 << vm->global->shift);
	int count = 0;
	assert(gc_fixedo(vm->global) && "Global must be fixed.");
	for (i = initial; i != k; ++i) {
		if (!i->flag) continue;
		gc_markv(&i->k);
		gc_markv(&i->v);
		++count;
	}
	return count;
}

static int gc_mark_context(struct ymd_mach *vm) {
	int count = 0;
	struct ymd_context *l = ioslate(vm);
	if (l->info) { // Mark all local variable
		int n = func_nlocal(l->info->run);
		struct variable *i, *k = l->info->loc + n;
		assert(l->info->loc);
		for (i = l->loc; i != k; ++i) {
			gc_markv(i);
			++count;
		}
	}
	if (l->info) { // Mark all function in stack
		struct call_info *i = l->info;
		while (i) {
			gc_marko(i->run);
			++count;
			i = i->chain;
		}
	}
	if (l->stk != l->top) { // Mark all stack variable
		struct variable *i, *k = l->top;
		for (i = l->stk; i != k; ++i) {
			gc_markv(i);
			++count;
		}
	}
	return count;
}

static void gc_move2gray(struct ymd_mach *vm) {
	struct gc_struct *gc = &vm->gc;
	struct gc_node dummy = { gc->alloced, 0, 0, 0, };
	struct gc_node *i = gc->alloced, *p = &dummy;
	assert (i != NULL && "No any alloced objects?");
	while (i) {
		if (gc_grayo(i)) {
			p->next = i->next;
			i->next = gc->gray;
			gc->gray = i;
			i = p->next;
		} else {
			p = i;
			i = i->next;
		}
	}
	gc->alloced = dummy.next; // Reset header node.
}

static void gc_mark_obj(struct gc_node *o) {
	if (!gc_whiteo(o))
		return;
	switch (o->type) {
	case T_KSTR:
		if (kstr_f(o)->len < MAX_KPOOL_LEN)
			gc_white2black(o);
		else
			gc_white2gray(o);
		break;
	case T_MAND:
	case T_FUNC:
	case T_DYAY:
	case T_HMAP:
	case T_SKLS:
		gc_white2gray(o);
		break;
	default:
		assert(!"No reached.");
		break;
	}
}

static void gc_travel_obj(struct gc_node *o) {
	mm_work(o);
	gc_gray2black(o);
	switch (o->type) {
	case T_KSTR:
		break;
	case T_MAND:
		if (mand_f(o)->proto) {
			gc_travelo(mand_f(o)->proto);
		}
		break;
	case T_FUNC:
		gc_travel_func(func_f(o));
		break;
	case T_DYAY: {
		int i;
		for (i = 0; i < dyay_f(o)->count; ++i) {
			gc_travelv(dyay_f(o)->elem + i);
		}
		} break;
	case T_HMAP: {
		struct kvi *initial = hmap_f(o)->item,
				   *i = NULL,
				   *k = initial + (1 << hmap_f(o)->shift);
		for (i = initial; i != k; ++i) {
			if (!i->flag) continue;
			gc_travelv(&i->k);
			gc_travelv(&i->v);
		}
		} break;
	case T_SKLS: {
		struct sknd *i;
		for (i = skls_f(o)->head->fwd[0]; i != NULL; i = i->fwd[0]) {
			gc_travelv(&i->k);
			gc_travelv(&i->v);
		}
		} break;
	default:
		assert (!"No reached.");
		break;
	}
	mm_idle(o);
}

static int gc_travel_func(struct func *o) {
	int i;
	struct chunk *core;
	gc_travelo(o->name);
	if (o->argv) {
		gc_travelo(o->argv);
	}
	if (o->upval) {
		for (i = 0; i < o->n_upval; ++i) {
			gc_travelv(o->upval + i);
		}
	}
	if (o->is_c)
		return 0;
	core = o->u.core;
	gc_travelo(core->file);
	for (i = 0; i < core->klz; ++i) {
		gc_travelo(core->lz[i]);
	}
	for (i = 0; i < core->kuz; ++i) {
		gc_travelo(core->uz[i]);
	}
	for (i = 0; i < core->kkval; ++i) {
		gc_travelv(core->kval + i);
	}
	return 0;
}

