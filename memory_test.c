#include "yut.h"
#include "state.h"
#include "memory.h"

int test_vm_alloc() {
	int i;
	int *baz, *buf = vm_zalloc(tvm, sizeof(int) * 16);
	ASSERT_NOTNULL(buf);
	for (i = 0; i < 16; ++i)
		ASSERT_EQ(int, buf[i], 0);
	vm_free(tvm, buf);

	buf = vm_zalloc(tvm, sizeof(int) * 4096);
	ASSERT_NOTNULL(buf);
	for (i = 0; i < 4096; ++i)
		ASSERT_EQ(int, buf[i], 0);
	vm_free(tvm, buf);

	buf = vm_zalloc(tvm, sizeof(int) * 32);
	for (i = 0; i < 32; ++i)
		ASSERT_EQ(int, buf[i], 0);
	baz = vm_realloc(tvm, buf, sizeof(int) * 4096);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < 32; ++i)
		ASSERT_EQ(int, baz[i], 0);
	
	vm_free(tvm, vm_zalloc(tvm, 16));

	buf = vm_realloc(tvm, baz, sizeof(int) * 1024 * 4096);
	ASSERT_FALSE(buf == baz);
	return 0;
}

int test_memory_managment() {
#define MM_PARAMS n, 16, sizeof(int)
	int i, n;
	int *baz, *buf = vm_zalloc(tvm, sizeof(int) * 16);

	n = 14;
	for (i = 0; i < n; ++i)
		buf[i] = i;
	baz = mm_need(tvm, buf, MM_PARAMS);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < n; ++i)
		ASSERT_EQ(int, baz[i], i);

	n = 16;
	buf = mm_need(tvm, baz, MM_PARAMS);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < 14; ++i)
		ASSERT_EQ(int, buf[i], i);
	ASSERT_EQ(int, buf[14], 0);
	ASSERT_EQ(int, buf[15], 0);

	baz = mm_shrink(tvm, buf, MM_PARAMS);
	ASSERT_TRUE(buf == baz);

	n = 4;
	buf = mm_shrink(tvm, baz, MM_PARAMS);
	ASSERT_FALSE(buf == baz);
	for (i = 0; i < n; ++i)
		ASSERT_EQ(int, buf[i], i);
#undef MM_PARAMS
	mm_free(tvm, buf, n, sizeof(int));
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(vm_alloc, normal)
	TEST_ENTRY(memory_managment, normal)
TEST_END
