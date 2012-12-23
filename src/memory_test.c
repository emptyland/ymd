#include "state.h"
#include "memory.h"
#include "memory_test.def"

static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, +1);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, -1);
	ymd_final(vm);
}

static int test_vm_alloc(struct ymd_mach *vm) {
	int i;
	int *baz, *buf = vm_zalloc(vm, sizeof(int) * 16);
	ASSERT_NOTNULL(buf);
	for (i = 0; i < 16; ++i)
		ASSERT_EQ(int, buf[i], 0);
	vm_free(vm, buf);

	buf = vm_zalloc(vm, sizeof(int) * 4096);
	ASSERT_NOTNULL(buf);
	for (i = 0; i < 4096; ++i)
		ASSERT_EQ(int, buf[i], 0);
	vm_free(vm, buf);

	buf = vm_zalloc(vm, sizeof(int) * 32);
	for (i = 0; i < 32; ++i)
		ASSERT_EQ(int, buf[i], 0);
	baz = vm_realloc(vm, buf, sizeof(int) * 4096);
	for (i = 0; i < 32; ++i)
		ASSERT_EQ(int, baz[i], 0);
	
	vm_free(vm, vm_zalloc(vm, 16));

	buf = vm_realloc(vm, baz, sizeof(int) * 1024 * 4096);
	ASSERT_FALSE(buf == baz);
	return 0;
}

static int test_memory_managment(struct ymd_mach *vm) {
#define MM_PARAMS n, 16, sizeof(int)
	int i, n;
	int *baz, *buf = vm_zalloc(vm, sizeof(int) * 16);

	n = 14;
	for (i = 0; i < n; ++i)
		buf[i] = i;
	baz = mm_need(vm, buf, MM_PARAMS);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < n; ++i)
		ASSERT_EQ(int, baz[i], i);

	n = 16;
	buf = mm_need(vm, baz, MM_PARAMS);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < 14; ++i)
		ASSERT_EQ(int, buf[i], i);
	ASSERT_EQ(int, buf[14], 0);
	ASSERT_EQ(int, buf[15], 0);

	baz = mm_shrink(vm, buf, MM_PARAMS);
	ASSERT_TRUE(buf == baz);

	n = 4;
	buf = mm_shrink(vm, baz, MM_PARAMS);
	ASSERT_FALSE(buf == baz);
	for (i = 0; i < n; ++i)
		ASSERT_EQ(int, buf[i], i);
#undef MM_PARAMS
	mm_free(vm, buf, n, sizeof(int));
	return 0;
}

