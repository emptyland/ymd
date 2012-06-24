#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>

#define GC_ENTER   1
#define GC_GRABED  2
#define GC_MARK    4

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
void *gc_new(struct gc_struct *gc, size_t size, unsigned char type);
int gc_init(struct gc_struct *gc, int k);
void gc_final(struct gc_struct *gc);
// Tracked Alloc/Release
void *gc_zalloc(struct gc_struct *gc, size_t size);
void *gc_realloc(struct gc_struct *gc, void *chunk, size_t old,
                 size_t size);
void gc_free(struct gc_struct *gc, void *chunk, size_t size);

// Memory managemant functions:
void *mm_need(void *raw, int n, int align, size_t chunk);
void *mm_shrink(void *raw, int n, int align, size_t chunk);

// Reference count management:
static inline void *mm_grab(void *p) {
	int *ref = p; ++(*ref); return p;
}
void mm_drop(void *p);

#endif // YMD_MEMORY_H
