#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "builtin.h"

struct ymd_mach;

#define GC_HEAD             \
	struct gc_node *next;   \
	unsigned char reserved; \
	unsigned char type;     \
	unsigned short marked

#define GC_WHITE0 0
#define GC_WHITE1 1
#define GC_GRAY   2
#define GC_BLACK  3
#define GC_FIXED  4
#define GC_MASK   0x0ff
#define GC_BUSY   0x100

#define GC_PAUSE       0
#define GC_PROPAGATE   1
#define GC_SWEEPSTRING 2
#define GC_SWEEP       3
#define GC_FINALIZE    4

#define gcx(obj)     ((struct gc_node *)(obj))

#define gc_otherwhite(white) ((white) == GC_WHITE0 ? GC_WHITE1 : GC_WHITE0)

#define gc_grayo(o)   (gc_mask(o) == GC_GRAY)
#define gc_fixedo(o)  (gc_mask(o) == GC_FIXED)
#define gc_whiteo(o)  (gc_mask(o) <= GC_WHITE1)
#define gc_blacko(o)  (gc_mask(o) == GC_BLACK)

#define gc_mask(o)    ((o)->marked & GC_MASK)

struct gc_node {
	GC_HEAD;
};

struct gc_struct {
	struct gc_node *alloced; // allocated objects list
	struct gc_node *gray; // gray nodes
	struct gc_node *gray_again; // all of gray nodes for atomic mark
	int white; // current white
	int n_alloced; // number of allocated objects
	size_t threshold; // > threshold then full gc
	size_t used; // used bytes
	size_t point; // save used bytes in gc beginning
	long long last; // last full gc tick number
	int state; // current state
	int pause; // pause counter
	int fixed;  // number of fixed objects
	int sweep_kpool; // sweeping in kpool's index
	struct gc_node *sweep; // sweep object list
	int sweep_step; // number of sweeping step
	FILE *logf; // gc log file
};

// GC functions:
int gc_init(struct ymd_mach *vm, int k);
void gc_final(struct ymd_mach *vm);

// Run GC by one step.
int gc_step(struct ymd_mach *vm);

// New a GC managed object.
void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type);
void gc_del(struct ymd_mach *vm, void *p);

// Set GC pasue or running
int gc_active(struct ymd_mach *vm, int off);

// Memory managemant functions:
void *mm_zalloc(struct ymd_mach *vm, int n, size_t chunk);
void *mm_realloc(struct ymd_mach *vm, void *raw, int old, int n,
		size_t chunk);
void mm_free(struct ymd_mach *vm, void *raw, int n, size_t chunk);
void *mm_need(struct ymd_mach *vm, void *raw, int n, int align,
		size_t chunk);
void *mm_shrink(struct ymd_mach *vm, void *raw, int n, int align,
		size_t chunk);

// Reference count management:
static YMD_INLINE void *mm_grab(void *p) {
	int *ref = p;
	++(*ref);
	return p;
}

static YMD_INLINE void mm_drop(struct ymd_mach *vm, void *p, size_t size) {
	int *ref = p;
	assert(*ref > 0 && "Bad referened number.");
	--(*ref);
	if (*ref == 0) mm_free(vm, p, 1, size);
}

#define mm_busy(o)  ((o)->marked & GC_BUSY)

#if defined(NDEBUG)
#	define mm_work(o) do { \
		assert (!((o)->marked & GC_BUSY) && "Don't grab a object again."); \
		(o)->marked |= GC_BUSY; \
	} while(0)

#	define mm_idle(o) do { \
		assert (((o)->marked & GC_BUSY) && "Don't drop a object again."); \
		(o)->marked &= ~GC_BUSY; \
	} while(0)
#else
#	define mm_work(o) ((o)->marked |= GC_BUSY)
#	define mm_idle(o) ((o)->marked &= ~GC_BUSY)
#endif

#endif // YMD_MEMORY_H
