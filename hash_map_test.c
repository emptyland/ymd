#include "core.h"
#include "yut_rand.h"
#include "yut.h"

struct hmap *map;
struct hmap *benchmark;

static int test_hmap_creation_1() {
	struct variable k, *rv;

	k.type = T_INT;
	k.u.i = 1024;
	rv = hmap_put(tvm, map, &k);
	rv->type = T_KSTR;
	rv->u.ref = gcx(kstr_fetch(tvm, "1024", -1));

	rv = hmap_put(tvm, map, &k);
	ASSERT_EQ(uint, rv->type, T_KSTR);
	ASSERT_STREQ(kstr_of(ioslate(tvm), rv)->land, "1024");

	k.type = T_KSTR;
	k.u.ref = gcx(kstr_fetch(tvm, "1024", -1));
	rv = hmap_put(tvm, map, &k);
	rv->type = T_INT;
	rv->u.i = 1024;

	rv = hmap_get(map, &k);
	ASSERT_EQ(uint, rv->type, T_INT);
	ASSERT_EQ(large, rv->u.i, 1024);

	vset_int(&k, 1024);
	ASSERT_TRUE(hmap_remove(tvm, map, &k));
	ASSERT_TRUE(knil == hmap_get(map, &k));
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
			k.u.ref = gcx(kstr_fetch(tvm, kz->land, kz->len));
			rv = hmap_put(tvm, map, &k);
			rv->type = T_INT;
			rv->u.i = i;
		}
	RAND_END
	return 0;
}

static int test_hmap_insertion_1 () {
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(insertion)
	while (i--) {
		struct variable k;
		vset_int(&k, i);
		vset_int(hmap_put(tvm, map, &k), i);
	}
	TIME_RECORD_END
	return 0;
}

static int test_hmap_insertion_2 () {
	int i = 10000;
	TIME_RECORD_BEGIN(insertion2)
	while (i--) {
		int j = 16;
		while (j--) {
			struct hmap *x = hmap_new(tvm, 0);
			struct variable k;
			vset_int(&k, i);
			vset_int(hmap_put(tvm, x, &k), i);
		}
	}
	TIME_RECORD_END
	return 0;
}

static int test_hmap_search_1 () {
	struct variable k, *rv;
	char buf[32];
	int i = BENCHMARK_COUNT;
	while (i--) {
		snprintf(buf, sizeof(buf), "%d", i);
		k.type = T_KSTR;
		k.u.ref = gcx(kstr_fetch(tvm, buf, -1));
		rv = hmap_put(tvm, map, &k);
		rv->type = T_INT;
		rv->u.i = i;
	}
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		TIME_RECORD_BEGIN(searching)
		while (i--) {
			unsigned int index = RAND_RANGE(uint, 0, BENCHMARK_COUNT);
			snprintf(buf, sizeof(buf), "%u", index);
			k.type = T_KSTR;
			k.u.ref = gcx(kstr_fetch(tvm, buf, -1));
			rv = hmap_get(map, &k);
			ASSERT_EQ(uint, rv->type, T_INT);
			ASSERT_EQ(large, rv->u.i, index);
		}
		TIME_RECORD_END
	RAND_END
	return 0;
}

static int test_hmap_search_2 () {
	int i = 16;
	struct variable k;
	while (i--) {
		vset_int(&k, i);
		vset_int(hmap_put(tvm, map, &k), i);
	}
	i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(searching2)
	while (i--) {
		int j = 16;
		while (j--) {
			vset_int(&k, j);
			ASSERT_EQ(large, hmap_get(map, &k)->u.i, j);
		}
	}
	TIME_RECORD_END
	return 0;
}

static struct hmap *build_hmap(struct hmap *x, const int *raw, long i) {
	while (i--) {
		struct variable k, *rv;
		k.type = T_INT;
		k.u.i = raw[i];
		rv = hmap_put(tvm, x, &k);
		rv->type = T_INT;
		rv->u.i = i;
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
	rhs = build_hmap(hmap_new(tvm, -1), raw, k);
	free(raw);

	EXPECT_TRUE(hmap_equals(map, map));
	TIME_RECORD_BEGIN(equals)
	EXPECT_TRUE(hmap_equals(map, rhs));
	TIME_RECORD_END
	
	EXPECT_EQ(int, hmap_compare(map, map), 0);
	TIME_RECORD_BEGIN(compare)
	EXPECT_EQ(int, hmap_compare(map, rhs), 0);
	TIME_RECORD_END
	
	rhs = hmap_new(tvm, -1);
	EXPECT_FALSE(hmap_equals(map, rhs));
	EXPECT_GT(int, hmap_compare(map, rhs), 0);
	return 0;
}

static int test_hmap_removing () {
	struct variable k;
	vset_int(&k, 4);
	vset_int(hmap_put(tvm, map, &k), 0);
	vset_int(&k, 14);
	vset_int(hmap_put(tvm, map, &k), 1);
	vset_int(&k, 1024);
	vset_int(hmap_put(tvm, map, &k), 3);

	vset_int(&k, 4);
	ASSERT_TRUE(hmap_remove(tvm, map, &k));
	ASSERT_TRUE(knil == hmap_get(map, &k));
	vset_int(&k, 1024);
	ASSERT_TRUE(hmap_remove(tvm, map, &k));
	ASSERT_TRUE(knil == hmap_get(map, &k));
	vset_int(&k, 14);
	ASSERT_TRUE(hmap_remove(tvm, map, &k));
	ASSERT_TRUE(knil == hmap_get(map, &k));

	vset_int(&k, 1024);
	vset_int(hmap_put(tvm, map, &k), 1000);
	ASSERT_EQ(uint,  hmap_get(map, &k)->type, T_INT);
	ASSERT_EQ(large, hmap_get(map, &k)->u.i,  1000LL);

	vset_int(&k, 14);
	ASSERT_TRUE(hmap_remove(tvm, map, &k));
	return 0;
}

static int hmap_setup() {
	map = hmap_new(tvm, -1);
	gc_active(tvm, GC_PAUSE);
	return 0;
}

static void hmap_teardown() {
	gc_active(tvm, GC_IDLE);
}

TEST_BEGIN_WITH(hmap_setup, hmap_teardown)
	TEST_ENTRY(hmap_creation_1, normal)
	TEST_ENTRY(hmap_creation_2, benchmark)
	TEST_ENTRY(hmap_insertion_1, benchmark)
	TEST_ENTRY(hmap_insertion_2, benchmark)
	TEST_ENTRY(hmap_search_1, benchmark)
	TEST_ENTRY(hmap_search_2, benchmark)
	TEST_ENTRY(hmap_comparation, benchmark)
	TEST_ENTRY(hmap_removing, normal)
TEST_END

