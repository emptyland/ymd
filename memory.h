#ifndef YMD_MEMORY_H
#define YMD_MEMORY_H

#include <stdlib.h>

#define GC_WHITE 0
#define GC_GRAY  1
#define GC_BLACK 2

#define GC_HEAD           \
	struct gc_node *next; \
	unsigned char color;  \
	unsigned char type

struct gc_node {
	GC_HEAD;
};

struct gc_struct {
	struct hmap *root;
	struct gc_node **alloced;
	struct gc_node **weak;
	int n_alloced;
	int shift;
	int k_alloced;
};

// GC functions:
void *gc_alloc(struct gc_struct *gc, size_t size, unsigned char type);
int gc_init(struct gc_struct *gc, int k);
void gc_final(struct gc_struct *gc);

// Memory managemant functions:
void *mm_need(void *raw, int n, int align, size_t chunk);
void *mm_shrink(void *raw, int n, int align, size_t chunk);

#endif // YMD_MEMORY_H
