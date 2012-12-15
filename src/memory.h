#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

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
#define GC_GRAB   0x100

#define gcx(obj)     ((struct gc_node *)(obj))

#define GC_PAUSE       0
#define GC_PROPAGATE   1
#define GC_SWEEPSTRING 2
#define GC_SWEEP       3
#define GC_FINALIZE    4

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
int gc_step(struct ymd_mach *vm); // Run gc in one step.
void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type);
void gc_del(struct ymd_mach *vm, void *p);
int gc_active(struct ymd_mach *vm, int off);

// Memory managemant functions:
// Maybe run gc processing.
void *mm_zalloc(struct ymd_mach *vm, int n, size_t chunk);
void *mm_realloc(struct ymd_mach *vm, void *raw, int old, int n,
                 size_t chunk);
void mm_free(struct ymd_mach *vm, void *raw, int n, size_t chunk);

void *mm_need(struct ymd_mach *vm, void *raw, int n, int align,
              size_t chunk);
void *mm_shrink(struct ymd_mach *vm, void *raw, int n, int align,
                size_t chunk);

// Reference count management:
static inline void *mm_grab(void *p) {
	int *ref = p; ++(*ref); return p;
}

void mm_drop(struct ymd_mach *vm, void *p, size_t size);

#define gc_grabed(o)  ((o)->marked & GC_GRAB)

static inline void gc_grabo(struct gc_node *o) {
	assert (!(o->marked & GC_GRAB) && "Don't grab a object again.");
	o->marked |= GC_GRAB;
}

static inline void gc_dropo(struct gc_node *o) {
	assert ((o->marked & GC_GRAB) && "Don't drop a object again.");
	o->marked &= ~GC_GRAB;
}

#endif // YMD_MEMORY_H
