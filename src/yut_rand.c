#include "yut_rand.h"
#include "yut_type.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <assert.h>

struct yut_tracked {
	struct yut_tracked *next;
	struct yut_kstr core;
};

static struct yut_rand_context *top;

static YUT_INLINE long long initial_rand_seed() {
	long long seed;
	int rv, fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		goto bad_seed;
	rv = read(fd, &seed, sizeof(seed));
	if (rv != sizeof(seed))
		goto bad_seed;
	return seed;
bad_seed:
	return (12345678LL ^ ((int)time(NULL)));
}

void yut_init_rand(struct yut_rand_context *self, int model) {
	self->model = model;
	self->chain = top;
	self->head = NULL;
	self->seed = initial_rand_seed();
	self->n = 0x7ffffffffLL;
	top = self;
}

void yut_final_rand() {
	struct yut_tracked *i = top->head, *p = i;
	assert(top != NULL);
	i = top->head;
	while (i) {
		p = i;
		i = i->next;
		free(p);
	}
	top = top->chain;
}

long long yut_rand() {
	struct yut_rand_context *self = top;
	long long rv;
	assert(self != NULL);
	rv = (self->n * self->n) >> 16;
	rv ^= (self->seed);
	self->n = (self->n * 37156667LL + 43112609LL) % 0x7fffffffffffffff;
	return rv;
}

const struct yut_kstr *yut_randz(int bin, int min, int max) {
	int i = RAND_RANGE(uint, min, max);
	struct yut_tracked *x = calloc(
		sizeof(*x) + i, 1);
	x->core.len = i;
	if (bin) {
		while (i--)
			x->core.land[i] = (char)RAND_RANGE(uint, 0, 255);
	} else {
		while (i--)
			x->core.land[i] = (char)RAND_RANGE(uint, 32, 126);
	}
	x->next = top->head;
	top->head = x;
	return &x->core;
}
