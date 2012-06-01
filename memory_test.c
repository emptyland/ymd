#include "yut.h"
#include "state.h"
#include "memory.h"

int test_vm_alloc() {
	int i;
	int *baz, *buf = vm_zalloc(sizeof(int) * 16);
	ASSERT_NOTNULL(buf);
	for (i = 0; i < 16; ++i)
		ASSERT_EQ(int, buf[i], 0);
	vm_free(buf);

	buf = vm_zalloc(sizeof(int) * 4096);
	ASSERT_NOTNULL(buf);
	for (i = 0; i < 4096; ++i)
		ASSERT_EQ(int, buf[i], 0);
	vm_free(buf);

	buf = vm_zalloc(sizeof(int) * 32);
	for (i = 0; i < 32; ++i)
		ASSERT_EQ(int, buf[i], 0);
	baz = vm_realloc(buf, sizeof(int) * 4096);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < 32; ++i)
		ASSERT_EQ(int, baz[i], 0);
	
	vm_free(vm_zalloc(16));

	buf = vm_realloc(baz, sizeof(int) * 1024 * 4096);
	ASSERT_FALSE(buf == baz);
	return 0;
}

int test_memory_managment() {
#define MM_PARAMS n, 16, sizeof(int)
	int i, n;
	int *baz, *buf = vm_zalloc(sizeof(int) * 16);

	n = 14;
	for (i = 0; i < n; ++i)
		buf[i] = i;
	baz = mm_need(buf, MM_PARAMS);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < n; ++i)
		ASSERT_EQ(int, baz[i], i);

	n = 16;
	buf = mm_need(baz, MM_PARAMS);
	ASSERT_TRUE(buf == baz);
	for (i = 0; i < 14; ++i)
		ASSERT_EQ(int, buf[i], i);
	ASSERT_EQ(int, buf[14], 0);
	ASSERT_EQ(int, buf[15], 0);

	baz = mm_shrink(buf, MM_PARAMS);
	ASSERT_TRUE(buf == baz);

	n = 4;
	buf = mm_shrink(baz, MM_PARAMS);
	ASSERT_FALSE(buf == baz);
	for (i = 0; i < n; ++i)
		ASSERT_EQ(int, buf[i], i);
#undef MM_PARAMS
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(vm_alloc, normal)
	TEST_ENTRY(memory_managment, normal)
TEST_END
