#include "core.h"
#include "yut_rand.h"
#include "testing/dynamic_array_test.def"

#define gc(p) ((struct gc_node *)(p))

//static struct dyay *arr;
static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, +1);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, -1);
	ymd_final(vm);
}

static int test_dyay_creation_1 (struct ymd_mach *vm) {
	struct dyay *arr = dyay_new(vm, 0);
	struct variable *rv;

	ASSERT_EQ(uint, arr->count, 0);
	ASSERT_EQ(uint, arr->max, 0);

	rv = dyay_add(vm, arr);
	ASSERT_NOTNULL(rv);
	ASSERT_FALSE(rv == knil);
	ASSERT_EQ(int, ymd_type(rv), T_NIL);
	ASSERT_EQ(large, rv->u.i, 0LL);
	ASSERT_EQ(uint, arr->count, 1);
	ASSERT_EQ(uint, arr->max, 16);

	arr = dyay_new(vm, 13);
	ASSERT_NOTNULL(arr);
	ASSERT_EQ(uint, arr->count, 0);
	ASSERT_EQ(uint, arr->max, 13);
	return 0;
}

static int test_dyay_addition_1 (struct ymd_mach *vm) {
	struct dyay *arr = dyay_new(vm, 0);
	struct variable *rv;
	static const int k = 41;
	int i = k;

	ASSERT_EQ(uint, arr->count, 0);
	ASSERT_EQ(uint, arr->max, 0);

	while (i--) {
		rv = dyay_add(vm, arr);
		ASSERT_NOTNULL(rv);
		ASSERT_FALSE(rv == knil);
		ASSERT_EQ(int, ymd_type(rv), T_NIL);
		ASSERT_EQ(large, rv->u.i, 0LL);
		ASSERT_EQ(uint, arr->count, k - i);

		rv->tt = T_INT;
		rv->u.i = k - i - 1;
	}
	ASSERT_EQ(uint, arr->max, (arr->count - 1) * 3 / 2 + 16);
	i = k;
	while (i--) {
		rv = dyay_get(arr, i);
		ASSERT_EQ(int, ymd_type(rv), T_INT);
		ASSERT_EQ(large, rv->u.i, i);
	}
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_dyay_addition_2 (struct ymd_mach *vm) {
	struct dyay *arr = dyay_new(vm, 0);
	struct variable *rv;
	int i = BENCHMARK_COUNT;
	TIME_RECORD_BEGIN(addition)
	while (i--) {
		rv = dyay_add(vm, arr);
		rv->tt = T_INT;
		rv->u.i = BENCHMARK_COUNT - i - 1;
	}
	TIME_RECORD_END
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			size_t index = RAND_RANGE(ularge, 0, BENCHMARK_COUNT);
			rv = dyay_get(arr, index);
			ASSERT_EQ(int, ymd_type(rv), T_INT);
			ASSERT_EQ(large, rv->u.i, index);
		}
	RAND_END
	return 0;
}

static int test_dyay_addition_3 (struct ymd_mach *vm) {
	int i = BENCHMARK_COUNT;

	TIME_RECORD_BEGIN(addition3)
	while (i--) {
		int j = 16;
		while (j--) {
			struct dyay *x = dyay_new(vm, 0);
			setv_int(dyay_add(vm, x), j);
		}
	}
	TIME_RECORD_END
	return 0;
}

static void add_int (struct dyay *dya, ymd_int_t i,
		struct ymd_mach *vm) {
	struct variable *rv = dyay_add(vm, dya);
	rv->tt = T_INT;
	rv->u.i = i;
}

static int test_dyay_comparation (struct ymd_mach *vm) {
	struct dyay *arr = dyay_new(vm, 0);
	struct dyay *rhs = dyay_new(vm, 0);

	add_int(arr, 0, vm);
	add_int(arr, 1, vm);
	add_int(rhs, 0, vm);
	ASSERT_GT(int, dyay_compare(arr, rhs), 0);
	ASSERT_FALSE(dyay_equals(arr, rhs));

	add_int(rhs, 1, vm);
	ASSERT_EQ(int, dyay_compare(arr, rhs), 0);
	ASSERT_TRUE(dyay_equals(arr, rhs));

	add_int(rhs, 0, vm);
	ASSERT_LT(int, dyay_compare(arr, rhs), 0);
	ASSERT_FALSE(dyay_equals(arr, rhs));

	ASSERT_TRUE(dyay_equals(arr, arr));
	ASSERT_EQ(int, dyay_compare(arr, arr), 0);
	ASSERT_TRUE(dyay_equals(rhs, rhs));
	ASSERT_EQ(int, dyay_compare(rhs, rhs), 0);
	return 0;
}

