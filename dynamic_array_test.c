#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

#define gc(p) ((struct gc_node *)(p))

static struct dyay *arr;

static int test_dyay_creation_1() {
	struct variable *rv;

	ASSERT_EQ(uint, arr->count, 0);
	ASSERT_EQ(uint, arr->max, 0);

	rv = dyay_add(tvm, arr);
	ASSERT_NOTNULL(rv);
	ASSERT_FALSE(rv == knil);
	ASSERT_EQ(uint, rv->type, T_NIL);
	ASSERT_EQ(large, rv->value.i, 0LL);
	ASSERT_EQ(uint, arr->count, 1);
	ASSERT_EQ(uint, arr->max, 16);

	arr = dyay_new(tvm, 13);
	ASSERT_NOTNULL(arr);
	ASSERT_EQ(uint, arr->count, 0);
	ASSERT_EQ(uint, arr->max, 13);
	return 0;
}

static int test_dyay_addition_1() {
	struct variable *rv;
	static const int k = 41;
	int i = k;

	ASSERT_EQ(uint, arr->count, 0);
	ASSERT_EQ(uint, arr->max, 0);

	while (i--) {
		rv = dyay_add(tvm, arr);
		ASSERT_NOTNULL(rv);
		ASSERT_FALSE(rv == knil);
		ASSERT_EQ(uint, rv->type, T_NIL);
		ASSERT_EQ(large, rv->value.i, 0LL);
		ASSERT_EQ(uint, arr->count, k - i);

		rv->type = T_INT;
		rv->value.i = k - i - 1;
	}
	ASSERT_EQ(uint, arr->max, (arr->count - 1) * 3 / 2 + 16);
	i = k;
	while (i--) {
		rv = dyay_get(arr, i);
		ASSERT_EQ(uint, rv->type, T_INT);
		ASSERT_EQ(large, rv->value.i, i);
	}
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_dyay_addition_2() {
	struct variable *rv;
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(addition)
	while (i--) {
		rv = dyay_add(tvm, arr);
		rv->type = T_INT;
		rv->value.i = BENCHMARK_COUNT - i - 1;
	}
	TIME_RECORD_END
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			size_t index = RAND_RANGE(ularge, 0, BENCHMARK_COUNT);
			rv = dyay_get(arr, index);
			ASSERT_EQ(uint, rv->type, T_INT);
			ASSERT_EQ(large, rv->value.i, index);
		}
	RAND_END
	return 0;
}

static void add_int(struct dyay *dya, ymd_int_t i) {
	struct variable *rv = dyay_add(tvm, dya);
	rv->type = T_INT;
	rv->value.i = i;
}

static int test_dyay_comparation() {
	struct dyay *rhs = dyay_new(tvm, 0);

	add_int(arr, 0);
	add_int(arr, 1);
	add_int(rhs, 0);
	ASSERT_GT(int, dyay_compare(arr, rhs), 0);
	ASSERT_FALSE(dyay_equals(arr, rhs));

	add_int(rhs, 1);
	ASSERT_EQ(int, dyay_compare(arr, rhs), 0);
	ASSERT_TRUE(dyay_equals(arr, rhs));

	add_int(rhs, 0);
	ASSERT_LT(int, dyay_compare(arr, rhs), 0);
	ASSERT_FALSE(dyay_equals(arr, rhs));

	ASSERT_TRUE(dyay_equals(arr, arr));
	ASSERT_EQ(int, dyay_compare(arr, arr), 0);
	ASSERT_TRUE(dyay_equals(rhs, rhs));
	ASSERT_EQ(int, dyay_compare(rhs, rhs), 0);
	return 0;
}

static int test_dyay_setup() {
	arr = dyay_new(tvm, 0);
	gc_active(tvm, GC_PAUSE);
	return 0;
}

static void test_dyay_teardown() {
	arr = NULL;
	gc_active(tvm, GC_IDLE);
}

TEST_BEGIN_WITH(test_dyay_setup, test_dyay_teardown)
	TEST_ENTRY(dyay_creation_1, normal)
	TEST_ENTRY(dyay_addition_1, normal)
	TEST_ENTRY(dyay_addition_2, benchmark)
	TEST_ENTRY(dyay_comparation, normal)
TEST_END
