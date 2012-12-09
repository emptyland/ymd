#ifndef TEST_YUT_RAND_H
#define TEST_YUT_RAND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YUT_RAND_MOD_NORMAL  0
#define YUT_RAND_MOD_ADVANCE 1

#define RAND_BEGIN(model) { \
	struct yut_rand_context __intl_context; \
	yut_init_rand(&__intl_context, YUT_RAND_MOD_##model);

#define RAND_END \
	yut_final_rand(); \
}

#define RAND(type) type##_cast(yut_rand())

// Only unsigned integer ok!
#define RAND_RANGE(type, i, j) \
	type##_cast((i) + RAND(type) % ((j) - (i)))

#define RAND_STR() yut_randz(0, 0, 260)
#define RAND_BIN() yut_randz(1, 0, 260)
#define RANDz() ((const char *)RAND_STR()->land)
#define RANDx() ((const char *)RAND_BIN()->land)


struct yut_tracked;

struct yut_rand_context {
	struct yut_rand_context *chain;
	int model;
	long long seed;
	long long n;
	struct yut_tracked *head;
};

struct yut_kstr {
	int len;
	char land[1];
};

void yut_init_rand(struct yut_rand_context *, int);
void yut_final_rand();

long long yut_rand();
const struct yut_kstr *yut_randz(int bin, int min, int max);

#endif //TEST_YUT_RAND_H
