#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

#define gc(p) ((struct gc_node *)(p))

struct hmap *map;
struct hmap *benchmark;

static int test_hmap_creation_1() {
	struct variable k, *rv;

	k.type = T_INT;
	k.value.i = 1024;
	rv = hmap_get(map, &k);
	rv->type = T_KSTR;
	rv->value.ref = gc(kstr_new("1024", -1));

	rv = hmap_get(map, &k);
	ASSERT_EQ(uint, rv->type, T_KSTR);
	ASSERT_STREQ(kstr_of(rv)->land, "1024");

	k.type = T_KSTR;
	k.value.ref = gc(kstr_new("1024", -1));
	rv = hmap_get(map, &k);
	rv->type = T_INT;
	rv->value.i = 1024;

	rv = hmap_get(map, &k);
	ASSERT_EQ(uint, rv->type, T_INT);
	ASSERT_EQ(large, rv->value.i, 1024);
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_hmap_creation_2() {
	struct variable k, *rv;
	int i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			const struct yut_kstr *kz = RAND_STR();
			k.type = T_KSTR;
			k.value.ref = gc(kstr_new(kz->land, kz->len));
			rv = hmap_get(map, &k);
			rv->type = T_INT;
			rv->value.i = i;
		}
	RAND_END
	return 0;
}

static int test_hmap_search() {
	struct variable k, *rv;
	char buf[32];
	int i = BENCHMARK_COUNT;
	while (i--) {
		snprintf(buf, sizeof(buf), "%d", i);
		k.type = T_KSTR;
		k.value.ref = gc(kstr_new(buf, -1));
		rv = hmap_get(map, &k);
		rv->type = T_INT;
		rv->value.i = i;
	}
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		TIME_RECORD_BEGIN(searching)
		while (i--) {
			unsigned int index = RAND_RANGE(uint, 0, BENCHMARK_COUNT);
			snprintf(buf, sizeof(buf), "%u", index);
			k.type = T_KSTR;
			k.value.ref = gc(kstr_new(buf, -1));
			rv = hmap_get(map, &k);
			ASSERT_EQ(uint, rv->type, T_INT);
			ASSERT_EQ(large, rv->value.i, index);
		}
		TIME_RECORD_END
	RAND_END
	return 0;
}

static struct hmap *build_hmap(struct hmap *x, const int *raw, long i) {
	while (i--) {
		struct variable k, *rv;
		k.type = T_INT;
		k.value.i = raw[i];
		rv = hmap_get(x, &k);
		rv->type = T_INT;
		rv->value.i = i;
	}
	return x;
}

static int test_hmap_comparation() {
	struct hmap *rhs;
	int *raw = calloc(BENCHMARK_COUNT, sizeof(*raw));
	long k = BENCHMARK_COUNT, i = k;
	RAND_BEGIN(NORMAL)
		while (i--)
			raw[i] = RAND(int);
	RAND_END
	map = build_hmap(map, raw, k);
	rhs = build_hmap(hmap_new(-1), raw, k);
	free(raw);

	EXPECT_TRUE(hmap_equal(map, map));
	TIME_RECORD_BEGIN(equals)
	EXPECT_TRUE(hmap_equal(map, rhs));
	TIME_RECORD_END
	
	EXPECT_EQ(int, hmap_compare(map, map), 0);
	TIME_RECORD_BEGIN(compare)
	EXPECT_EQ(int, hmap_compare(map, rhs), 0);
	TIME_RECORD_END
	
	rhs = hmap_new(-1);
	EXPECT_FALSE(hmap_equal(map, rhs));
	EXPECT_LT(int, hmap_compare(map, rhs), 0);
	return 0;
}

static int test_hmap_setup() {
	map = hmap_new(-1);
	return 0;
}

TEST_BEGIN_WITH(test_hmap_setup, NULL)
	TEST_ENTRY(hmap_creation_1, normal)
	TEST_ENTRY(hmap_creation_2, benchmark)
	TEST_ENTRY(hmap_search, benchmark)
	TEST_ENTRY(hmap_comparation, benchmark)
TEST_END
