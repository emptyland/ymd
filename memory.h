#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>

struct ymd_mach;

#define GC_HEAD             \
	struct gc_node *next;   \
	unsigned char reserved; \
	unsigned char type;     \
	unsigned short marked

#define GC_BLACK_BIT0 1U
#define GC_GRAY_BIT0  (1U << 4)

enum gc_state {
	GC_PAUSE,
	GC_IDLE,
	GC_MARK,
	GC_SWEEP,
};

struct gc_node {
	GC_HEAD;
};

struct gc_struct {
	struct gc_node *alloced;
	int n_alloced;
	size_t threshold; // > threshold then full gc
	size_t used; // used bytes
	int pause;
};

#define gcx(obj) ((struct gc_node *)(obj))

// GC functions:
void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type);
int gc_active(struct ymd_mach *vm, int act);
int gc_init(struct ymd_mach *vm, int k);
void gc_final(struct ymd_mach *vm);

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
static inline void *mm_grab(void *p) {
	int *ref = p; ++(*ref); return p;
}

void mm_drop(struct ymd_mach *vm, void *p, size_t size);

#endif // YMD_MEMORY_H
