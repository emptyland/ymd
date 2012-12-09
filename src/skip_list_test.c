#include "core.h"
#include "yut_rand.h"
#include "testing/skip_list_test.def"

static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, GC_PAUSE);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, GC_IDLE);
	ymd_final(vm);
}

static int test_skls_creation_1 (struct ymd_mach *vm) {
	struct variable k1, k2, *rv;
	struct skls *ls = skls_new(vm);

	k1.type = T_INT;
	k1.u.i = 1024;
	rv = skls_put(vm, ls, &k1);
	rv->type = T_KSTR;
	rv->u.ref = gcx(kstr_fetch(vm, "1024", -1));

	k2.type = T_KSTR;
	k2.u.ref = gcx(kstr_fetch(vm, "1024", -1));
	rv = skls_put(vm, ls, &k2);
	rv->type = T_INT;
	rv->u.i = 1024;

	rv = skls_get(ls, &k1);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(uint, rv->type, T_KSTR);
	ASSERT_STREQ(kstr_of(ioslate(vm), rv)->land, "1024");

	rv = skls_get(ls, &k2);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(uint, rv->type, T_INT);
	ASSERT_EQ(large, rv->u.i, 1024LL);
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_skls_creation_2 (struct ymd_mach *vm) {
	struct variable k, *v;
	struct skls *ls = skls_new(vm);
	int i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			const struct yut_kstr *kz = RAND_STR();
			k.type = T_KSTR;
			k.u.ref = gcx(kstr_fetch(vm, kz->land, kz->len));
			v = skls_put(vm, ls, &k);
			v->type = T_INT;
			v->u.i = i;
		}
	RAND_END
	ASSERT_LE(int, ls->count, BENCHMARK_COUNT);
	return 0;
}

static int test_skls_sequence (struct ymd_mach *vm) {
	struct variable k, *v;
	struct skls *ls = skls_new(vm);
	struct sknd *x;
	int i = BENCHMARK_COUNT;
	while (i--) {
		k.type = T_INT;
		k.u.i = i;
		v = skls_put(vm, ls, &k);
		v->type = T_INT;
		v->u.i = BENCHMARK_COUNT - 1 - i;
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

static struct skls *build_skls (const int *raw, long i, struct ymd_mach *vm) {
	struct skls *ls = skls_new(vm);
	while (i--) {
		struct variable k, *v;
		k.type = T_INT;
		k.u.i = raw[i];
		v = skls_put(vm, ls, &k);
		v->type = T_INT;
		v->u.i = i;
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
		vset_int(&k, i);
		rv = skls_put(vm, list, &k);
		vset_int(rv, i);
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
			vset_int(&k, i);
			vset_int(skls_put(vm, x, &k), i);
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
		k.type = T_KSTR;
		k.u.ref = gcx(kstr_fetch(vm, buf, -1));
		rv = skls_put(vm, list, &k);
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
			k.u.ref = gcx(kstr_fetch(vm, buf, -1));
			rv = skls_get(list, &k);
			ASSERT_EQ(uint, rv->type, T_INT);
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
		vset_int(&k, i);
		vset_int(skls_put(vm, map, &k), i);
	}
	i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(searching2)
	while (i--) {
		int j = 16;
		while (j--) {
			vset_int(&k, j);
			ASSERT_EQ(large, skls_get(map, &k)->u.i, j);
		}
	}
	TIME_RECORD_END
	return 0;
}

static int test_skls_removing (struct ymd_mach *vm) {
	struct skls *map = skls_new(vm);
	struct variable k;
	vset_int(&k, 4);
	vset_int(skls_put(vm, map, &k), 0);
	vset_int(&k, 14);
	vset_int(skls_put(vm, map, &k), 1);
	vset_int(&k, 1024);
	vset_int(skls_put(vm, map, &k), 3);

	vset_int(&k, 4);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	ASSERT_TRUE(knil == skls_get(map, &k));
	vset_int(&k, 1024);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	ASSERT_TRUE(knil == skls_get(map, &k));
	vset_int(&k, 14);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	ASSERT_TRUE(knil == skls_get(map, &k));

	vset_int(&k, 1024);
	vset_int(skls_put(vm, map, &k), 1000);
	ASSERT_EQ(uint,  skls_get(map, &k)->type, T_INT);
	ASSERT_EQ(large, skls_get(map, &k)->u.i,  1000LL);
	vset_int(&k, 14);
	vset_int(skls_put(vm, map, &k), 10);
	ASSERT_EQ(uint,  skls_get(map, &k)->type, T_INT);
	ASSERT_EQ(large, skls_get(map, &k)->u.i,  10LL);
	vset_int(&k, 4);
	ASSERT_TRUE(skls_remove(vm, map, &k));
	return 0;
}

