#ifndef TEST_YUT_RAND_H
#define TEST_YUT_RAND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YUT_RAND_MOD_NORMAL  0
#define YUT_RAND_MOD_ADVANCE 1

#define RAND_BEIN(model) { \
	struct yut_rand_context __intl_context; \
	yut_rand_init(&__intl_context, YUT_RAND_MOD_##model);

#define RAND_END \
	yut_rand_final(&__intl_context); \
}

#define RAND(type)
#define RAND_RANGE(type, i, j)
#define RANDz()

struct yut_rand_context {
	struct yut_rand_context *chain;
	int model;
	int seed;
	int n;
	//struct ymd_thread_pool *pool;
};

long long yut_rand();

#endif //TEST_YUT_RAND_H
