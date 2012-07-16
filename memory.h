#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>

struct ymd_mach;

#define GC_HEAD             \
	struct gc_node *next;   \
	unsigned char reserved; \
	unsigned char type;     \
	unsigned short marked

struct gc_node {
	GC_HEAD;
};

struct gc_struct {
	struct hmap *root;
	struct gc_node *alloced;
	struct gc_node *weak;
	int n_alloced;
	size_t threshold; // > threshold then full gc
	size_t used; // used bytes
};

#define gcx(obj) ((struct gc_node *)(obj))

// GC functions:
void *gc_new(struct ymd_mach *vm, size_t size, unsigned char type);
int gc_init(struct ymd_mach *vm, int k);
void gc_final(struct ymd_mach *vm);

// Memory managemant functions:
void *mm_need(struct ymd_mach *vm, void *raw, int n, int align,
              size_t chunk);
void *mm_shrink(struct ymd_mach *vm, void *raw, int n, int align,
                size_t chunk);

// Reference count management:
static inline void *mm_grab(void *p) {
	int *ref = p; ++(*ref); return p;
}

void mm_drop(struct ymd_mach *vm, void *p);

#endif // YMD_MEMORY_H
