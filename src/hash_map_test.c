#include "core.h"
#include "yut_rand.h"
#include "testing/hash_map_test.def"

static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, GC_PAUSE);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, GC_IDLE);
	ymd_final(vm);
}

static int test_hmap_creation_1 (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0);
	struct variable k, *rv;

	setv_int(&k, 1024);
	setv_kstr(hmap_put(vm, map, &k), kstr_fetch(vm, "1024", -1));

	rv = hmap_put(vm, map, &k);
	ASSERT_EQ(int, TYPEV(rv), T_KSTR);
	ASSERT_STREQ(kstr_of(ioslate(vm), rv)->land, "1024");

	setv_kstr(&k, kstr_fetch(vm, "1024", -1));
	setv_int(hmap_put(vm, map, &k), 1024);

	rv = hmap_get(map, &k);
	ASSERT_EQ(int, TYPEV(rv), T_INT);
	ASSERT_EQ(large, rv->u.i, 1024);

	setv_int(&k, 1024);
	ASSERT_TRUE(hmap_remove(vm, map, &k));
	ASSERT_TRUE(knil == hmap_get(map, &k));
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_hmap_creation_2 (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0);
	struct variable k;
	int i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			const struct yut_kstr *kz = RAND_STR();
			setv_kstr(&k, kstr_fetch(vm, kz->land, kz->len));
			setv_int(hmap_put(vm, map, &k), i);
		}
	RAND_END
	return 0;
}

static int test_hmap_insertion_1 (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0);
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(insertion)
	while (i--) {
		struct variable k;
		setv_int(&k, i);
		setv_int(hmap_put(vm, map, &k), i);
	}
	TIME_RECORD_END
	return 0;
}

static int test_hmap_insertion_2 (struct ymd_mach *vm) {
	int i = 10000;
	TIME_RECORD_BEGIN(insertion2)
	while (i--) {
		int j = 16;
		while (j--) {
			struct hmap *x = hmap_new(vm, 0);
			struct variable k;
			setv_int(&k, i);
			setv_int(hmap_put(vm, x, &k), i);
		}
	}
	TIME_RECORD_END
	return 0;
}

static int test_hmap_search_1 (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0);
	struct variable k;
	char buf[32];
	int i = BENCHMARK_COUNT;
	while (i--) {
		snprintf(buf, sizeof(buf), "%d", i);
		setv_kstr(&k, kstr_fetch(vm, buf, -1));
		setv_int(hmap_put(vm, map, &k), i);
	}
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		TIME_RECORD_BEGIN(searching)
		while (i--) {
			struct variable *rv;
			unsigned int index = RAND_RANGE(uint, 0, BENCHMARK_COUNT);
			snprintf(buf, sizeof(buf), "%u", index);
			setv_kstr(&k, kstr_fetch(vm, buf, -1));
			rv = hmap_get(map, &k);
			ASSERT_EQ(int, TYPEV(rv), T_INT);
			ASSERT_EQ(large, rv->u.i, index);
		}
		TIME_RECORD_END
	RAND_END
	return 0;
}

static int test_hmap_search_2 (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0);
	int i = 16;
	struct variable k;
	while (i--) {
		setv_int(&k, i);
		setv_int(hmap_put(vm, map, &k), i);
	}
	i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(searching2)
	while (i--) {
		int j = 16;
		while (j--) {
			setv_int(&k, j);
			ASSERT_EQ(large, hmap_get(map, &k)->u.i, j);
		}
	}
	TIME_RECORD_END
	return 0;
}

static struct hmap *build_hmap(struct hmap *x, const int *raw, long i,
		struct ymd_mach *vm) {
	while (i--) {
		struct variable k;
		setv_int(&k, raw[i]);
		setv_int(hmap_put(vm, x, &k), i);
	}
	return x;
}

static int test_hmap_comparation (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0), *rhs;
	int *raw = calloc(BENCHMARK_COUNT, sizeof(*raw));
	long k = BENCHMARK_COUNT, i = k;
	RAND_BEGIN(NORMAL)
		while (i--)
			raw[i] = RAND(int);
	RAND_END
	map = build_hmap(map, raw, k, vm);
	rhs = build_hmap(hmap_new(vm, -1), raw, k, vm);
	free(raw);

	EXPECT_TRUE(hmap_equals(map, map));
	TIME_RECORD_BEGIN(equals)
	EXPECT_TRUE(hmap_equals(map, rhs));
	TIME_RECORD_END
	
	EXPECT_EQ(int, hmap_compare(map, map), 0);
	TIME_RECORD_BEGIN(compare)
	EXPECT_EQ(int, hmap_compare(map, rhs), 0);
	TIME_RECORD_END
	
	rhs = hmap_new(vm, -1);
	EXPECT_FALSE(hmap_equals(map, rhs));
	EXPECT_GT(int, hmap_compare(map, rhs), 0);
	return 0;
}

static int test_hmap_removing (struct ymd_mach *vm) {
	struct hmap *map = hmap_new(vm, 0);
	struct variable k;
	setv_int(&k, 4);
	setv_int(hmap_put(vm, map, &k), 0); // put 4: 0
	setv_int(&k, 14);
	setv_int(hmap_put(vm, map, &k), 1); // put 14: 1
	setv_int(&k, 1024);
	setv_int(hmap_put(vm, map, &k), 3); // put 1024: 3

	setv_int(&k, 4);
	ASSERT_TRUE(hmap_remove(vm, map, &k)); // rm 4
	ASSERT_TRUE(knil == hmap_get(map, &k));
	setv_int(&k, 1024);
	ASSERT_TRUE(hmap_remove(vm, map, &k)); // rm 1024
	ASSERT_TRUE(knil == hmap_get(map, &k));
	setv_int(&k, 14);
	ASSERT_TRUE(hmap_remove(vm, map, &k)); // rm 14
	ASSERT_TRUE(knil == hmap_get(map, &k));

	setv_int(&k, 1024);
	setv_int(hmap_put(vm, map, &k), 1000); // put 1024: 1000
	ASSERT_EQ(uint,  hmap_get(map, &k)->tt, T_INT);
	ASSERT_EQ(large, hmap_get(map, &k)->u.i,  1000LL);

	setv_int(&k, 14);
	ASSERT_FALSE(hmap_remove(vm, map, &k)); // rm 14
	return 0;
}

