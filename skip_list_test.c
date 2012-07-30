#include "core.h"
#include "yut_rand.h"
#include "yut.h"

static int test_skls_creation_1() {
	struct variable k1, k2, *rv;
	struct skls *ls = skls_new(tvm);

	k1.type = T_INT;
	k1.value.i = 1024;
	rv = skls_put(tvm, ls, &k1);
	rv->type = T_KSTR;
	rv->value.ref = gcx(kstr_fetch(tvm, "1024", -1));

	k2.type = T_KSTR;
	k2.value.ref = gcx(kstr_fetch(tvm, "1024", -1));
	rv = skls_put(tvm, ls, &k2);
	rv->type = T_INT;
	rv->value.i = 1024;

	rv = skls_get(ls, &k1);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(uint, rv->type, T_KSTR);
	ASSERT_STREQ(kstr_of(tvm, rv)->land, "1024");

	rv = skls_get(ls, &k2);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(uint, rv->type, T_INT);
	ASSERT_EQ(large, rv->value.i, 1024LL);
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_skls_creation_2() {
	struct variable k, *v;
	struct skls *ls = skls_new(tvm);
	int i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			const struct yut_kstr *kz = RAND_STR();
			k.type = T_KSTR;
			k.value.ref = gcx(kstr_fetch(tvm, kz->land, kz->len));
			v = skls_put(tvm, ls, &k);
			v->type = T_INT;
			v->value.i = i;
		}
	RAND_END
	ASSERT_LE(int, ls->count, BENCHMARK_COUNT);
	return 0;
}

static int test_skls_sequence() {
	struct variable k, *v;
	struct skls *ls = skls_new(tvm);
	struct sknd *x;
	int i = BENCHMARK_COUNT;
	while (i--) {
		k.type = T_INT;
		k.value.i = i;
		v = skls_put(tvm, ls, &k);
		v->type = T_INT;
		v->value.i = BENCHMARK_COUNT - 1 - i;
	}
	EXPECT_EQ(int, ls->count, BENCHMARK_COUNT);
	x = ls->head;
	ASSERT_NOTNULL(x);
	i = 0;
	while ((x = x->fwd[0]) != NULL)
		ASSERT_EQ(large, x->k.value.i, i++);
	EXPECT_EQ(int, i, BENCHMARK_COUNT);
	return 0;
}

static struct skls *build_skls(const int *raw, long i) {
	struct skls *ls = skls_new(tvm);
	while (i--) {
		struct variable k, *v;
		k.type = T_INT;
		k.value.i = raw[i];
		v = skls_put(tvm, ls, &k);
		v->type = T_INT;
		v->value.i = i;
	}
	return ls;
}

static int test_skls_comparation() {
	int raw[1024];
	struct skls *x, *rhs;
	long k = sizeof(raw) / sizeof(raw[0]), i = k;
	RAND_BEGIN(NORMAL)
		while (i--)
			raw[i] = RAND(int);
	RAND_END
	x = build_skls(raw, k);
	rhs = build_skls(raw, k);

	EXPECT_TRUE(skls_equals(x, x));
	EXPECT_TRUE(skls_equals(x, rhs));

	EXPECT_EQ(int, skls_compare(x, x), 0);
	EXPECT_EQ(int, skls_compare(x, rhs), 0);

	rhs = skls_new(tvm);
	EXPECT_FALSE(skls_equals(x, rhs));
	EXPECT_GT(int, skls_compare(x, rhs), 0);
	return 0;
}

static int test_skls_insertion_1 () {
	struct skls *list = skls_new(tvm);
	struct variable *rv;
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(insertion)
	while (i--) {
		struct variable k;
		vset_int(&k, i);
		rv = skls_put(tvm, list, &k);
		vset_int(rv, i);
	}
	TIME_RECORD_END
	return 0;
}

static int test_skls_insertion_2 () {
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(insertion2)
	while (i--) {
		int j = 16;
		while (j--) {
			struct skls *x = skls_new(tvm);
			struct variable k;
			vset_int(&k, i);
			vset_int(skls_put(tvm, x, &k), i);
		}
	}
	TIME_RECORD_END
	return 0;
}

static int test_skls_search_1() {
	struct skls *list = skls_new(tvm);
	struct variable k, *rv;
	char buf[32];
	int i = BENCHMARK_COUNT;
	while (i--) {
		snprintf(buf, sizeof(buf), "%d", i);
		k.type = T_KSTR;
		k.value.ref = gcx(kstr_fetch(tvm, buf, -1));
		rv = skls_put(tvm, list, &k);
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
			k.value.ref = gcx(kstr_fetch(tvm, buf, -1));
			rv = skls_get(list, &k);
			ASSERT_EQ(uint, rv->type, T_INT);
			ASSERT_EQ(large, rv->value.i, index);
		}
		TIME_RECORD_END
	RAND_END
	return 0;
}

static int test_skls_search_2 () {
	int i = 16;
	struct variable k;
	struct skls *map = skls_new(tvm);
	while (i--) {
		vset_int(&k, i);
		vset_int(skls_put(tvm, map, &k), i);
	}
	i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(searching2)
	while (i--) {
		int j = 16;
		while (j--) {
			vset_int(&k, j);
			ASSERT_EQ(large, skls_get(map, &k)->value.i, j);
		}
	}
	TIME_RECORD_END
	return 0;
}
static int skls_setup() {
	gc_active(tvm, GC_PAUSE);
	return 0;
}

static void skls_teardown() {
	gc_active(tvm, GC_IDLE);
}

TEST_BEGIN_WITH(skls_setup, skls_teardown)
	TEST_ENTRY(skls_creation_1, normal)
	TEST_ENTRY(skls_creation_2, benchmark)
	TEST_ENTRY(skls_insertion_1, benchmark)
	TEST_ENTRY(skls_insertion_2, benchmark)
	TEST_ENTRY(skls_sequence, benchmark)
	TEST_ENTRY(skls_comparation, normal)
	TEST_ENTRY(skls_search_1, benchmark)
	TEST_ENTRY(skls_search_2, benchmark)
TEST_END

