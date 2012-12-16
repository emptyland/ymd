#include "core.h"
#include "yut_rand.h"
#include "testing/skip_list_test.def"

static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, +1);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, -1);
	ymd_final(vm);
}

static int test_skls_creation_1 (struct ymd_mach *vm) {
	struct variable k1, k2, *rv;
	struct skls *ls = skls_new(vm);

	setv_int(&k1, 1024);
	setv_kstr(skls_put(vm, ls, &k1), kstr_fetch(vm, "1024", -1));

	setv_kstr(&k2, kstr_fetch(vm, "1024", -1));
	setv_int(skls_put(vm, ls, &k2), 1024);

	rv = skls_get(ls, &k1);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(int, ymd_type(rv), T_KSTR);
	ASSERT_STREQ(kstr_of(ioslate(vm), rv)->land, "1024");

	rv = skls_get(ls, &k2);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(int, ymd_type(rv), T_INT);
	ASSERT_EQ(large, rv->u.i, 1024LL);
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_skls_creation_2 (struct ymd_mach *vm) {
	struct skls *ls = skls_new(vm);
	int i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			const struct yut_kstr *kz = RAND_STR();
			struct variable k;
			setv_kstr(&k, kstr_fetch(vm, kz->land, kz->len));
			setv_int(skls_put(vm, ls, &k), i);
		}
	RAND_END
	ASSERT_LE(int, ls->count, BENCHMARK_COUNT);
	return 0;
}

static int test_skls_sequence (struct ymd_mach *vm) {
	struct skls *ls = skls_new(vm);
	struct sknd *x;
	int i = BENCHMARK_COUNT;
	while (i--) {
		struct variable k;
		setv_int(&k, i);
		setv_int(skls_put(vm, ls, &k), BENCHMARK_COUNT - 1 - i);
	}
	EXPECT_EQ(int, ls->count, BENCHMARK_COUNT);
	x = ls->head;
	ASSERT_NOTNULL(x);
	i = 0;
	while ((x = x->fwd[0]) != NULL)
		ASSERT_EQ(large, x->k.u.i, i++);
	EXPECT_EQ(int, i, BENCHMARK_COUNT);
	return 0;
}

static struct skls *build_skls (const int *raw, long i,
		struct ymd_mach *vm) {
	struct skls *ls = skls_new(vm);
	while (i--) {
		struct variable k;
		setv_int(&k, raw[i]);
		setv_int(skls_put(vm, ls, &k), i);
	}
	return ls;
}

static int test_skls_comparation (struct ymd_mach *vm) {
	int raw[1024];
	struct skls *x, *rhs;
	long k = sizeof(raw) / sizeof(raw[0]), i = k;
	RAND_BEGIN(NORMAL)
		while (i--)
			raw[i] = RAND(int);
	RAND_END
	x = build_skls(raw, k, vm);
	rhs = build_skls(raw, k, vm);

	EXPECT_TRUE(skls_equals(x, x));
	EXPECT_TRUE(skls_equals(x, rhs));

	EXPECT_EQ(int, skls_compare(x, x), 0);
	EXPECT_EQ(int, skls_compare(x, rhs), 0);

	rhs = skls_new(vm);
	EXPECT_FALSE(skls_equals(x, rhs));
	EXPECT_GT(int, skls_compare(x, rhs), 0);
	return 0;
}

static int test_skls_insertion_1 (struct ymd_mach *vm) {
	struct skls *list = skls_new(vm);
	struct variable *rv;
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(insertion)
	while (i--) {
		struct variable k;
		setv_int(&k, i);
		rv = skls_put(vm, list, &k);
		setv_int(rv, i);
	}
	TIME_RECORD_END
	return 0;
}

static int test_skls_insertion_2 (struct ymd_mach *vm) {
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(insertion2)
	while (i--) {
		int j = 16;
		while (j--) {
			struct skls *x = skls_new(vm);
			struct variable k;
			setv_int(&k, i);
			setv_int(skls_put(vm, x, &k), i);
		}
	}
	TIME_RECORD_END
	return 0;
}

static int test_skls_search_1 (struct ymd_mach *vm) {
	struct skls *list = skls_new(vm);
	struct variable k, *rv;
	char buf[32];
	int i = BENCHMARK_COUNT;
	while (i--) {
		snprintf(buf, sizeof(buf), "%d", i);
		setv_kstr(&k, kstr_fetch(vm, buf, -1));
		setv_int(skls_put(vm, list, &k), i);
	}
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		TIME_RECORD_BEGIN(searching)
		while (i--) {
			unsigned int index = RAND_RANGE(uint, 0, BENCHMARK_COUNT);
			snprintf(buf, sizeof(buf), "%u", index);
			setv_kstr(&k, kstr_fetch(vm, buf, -1));
			rv = skls_get(list, &k);
			ASSERT_EQ(int, rv->tt, T_INT);
			ASSERT_EQ(large, rv->u.i, index);
		}
		TIME_RECORD_END
	RAND_END
	return 0;
}

static int test_skls_search_2 (struct ymd_mach *vm) {
	int i = 16;
	struct variable k;
	struct skls *map = skls_new(vm);
	while (i--) {
		setv_int(&k, i);
		setv_int(skls_put(vm, map, &k), i);
	}
	i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(searching2)
	while (i--) {
		int j = 16;
		while (j--) {
			setv_int(&k, j);
			ASSERT_EQ(large, skls_get(map, &k)->u.i, j);
		}
	}
	TIME_RECORD_END
	return 0;
}

static int test_skls_removing (struct ymd_mach *vm) {
	struct skls *map = skls_new(vm);
	struct variable k;
	setv_int(&k, 4);
	setv_int(skls_put(vm, map, &k), 0);
	setv_int(&k, 14);
	setv_int(skls_put(vm, map, &k), 1);
	setv_int(&k, 1024);
	setv_int(skls_put(vm, map, &k), 3);

	setv_int(&k, 4);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	ASSERT_TRUE(knil == skls_get(map, &k));
	setv_int(&k, 1024);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	ASSERT_TRUE(knil == skls_get(map, &k));
	setv_int(&k, 14);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	ASSERT_TRUE(knil == skls_get(map, &k));

	setv_int(&k, 1024);
	setv_int(skls_put(vm, map, &k), 1000);
	ASSERT_EQ(int,  skls_get(map, &k)->tt, T_INT);
	ASSERT_EQ(large, skls_get(map, &k)->u.i,  1000LL);
	setv_int(&k, 14);
	setv_int(skls_put(vm, map, &k), 10);
	ASSERT_EQ(int,  skls_get(map, &k)->tt, T_INT);
	ASSERT_EQ(large, skls_get(map, &k)->u.i,  10LL);
	setv_int(&k, 4);
	ASSERT_FALSE(skls_remove(vm, map, &k));
	return 0;
}

