#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>
#include <stdio.h>

struct ymd_mach;

#define GC_HEAD             \
	struct gc_node *next;   \
	unsigned char reserved; \
	unsigned char type;     \
	unsigned short marked

#define GC_BLACK_BIT0 1U
#define GC_GRAY_BIT0  (1U << 4)

enum gc_state {
	GC_PAUSE, // gc->pause++ gc stop if pause > 0
	GC_IDLE,  // gc->pause-- gc run if pause == 0
	GC_MARK,  // execute mark processing
	GC_SWEEP, // execute sweep processing
};

struct gc_node {
	GC_HEAD;
};

struct gc_struct {
	struct gc_node *alloced; // allocated objects list
	int n_alloced; // number of allocated objects
	size_t threshold; // > threshold then full gc
	size_t used; // used bytes
	long long last; // last full gc tick number
	int pause; // pause counter
	FILE *logf; // gc log file
};

#define gcx(obj) ((struct gc_node *)(obj))

// GC functions:
int gc_init(struct ymd_mach *vm, int k);
void gc_final(struct ymd_mach *vm);
int gc_active(struct ymd_mach *vm, int act);
void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type);
void gc_del(struct ymd_mach *vm, void *p);

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

#endif // YMD_MEMORY_H
