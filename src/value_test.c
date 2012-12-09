#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut.h"

static int test_kstr_creation() {
	const struct kstr *kz = kstr_fetch(tvm, "abcdef", 6);
	ASSERT_NOTNULL(kz);
	ASSERT_EQ(int, kz->type, T_KSTR);
	ASSERT_STREQ(kz->land, "abcdef");
	ASSERT_EQ(int, kz->len, 6);

	kz = kstr_fetch(tvm, "abcdef", 3);
	ASSERT_STREQ(kz->land, "abc");
	ASSERT_EQ(int, kz->len, 3);

	kz = kstr_fetch(tvm, "abcdef", 0);
	ASSERT_STREQ(kz->land, "");
	ASSERT_EQ(int, kz->len, 0);

	kz = kstr_fetch(tvm, "abcdefgh", -1);
	ASSERT_STREQ(kz->land, "abcdefgh");
	ASSERT_EQ(int, kz->len, 8);
	return 0;
}

static int test_kstr_comparation_1() {
	const struct kstr *kz = kstr_fetch(tvm, "aaaaaa", -1);
	const struct kstr *zz = kstr_fetch(tvm, "aaaaaa", 6);

	ASSERT_TRUE(kz == zz);
	ASSERT_EQ(ulong, kz->hash, zz->hash);
	ASSERT_STREQ(kz->land, zz->land);
	ASSERT_TRUE(kstr_equals(kz, zz));
	ASSERT_EQ(int, kstr_compare(kz, zz), 0);

	kz = kstr_fetch(tvm, "", 0);
	zz = kstr_fetch(tvm, "", 0);

	ASSERT_TRUE(kz == zz);
	ASSERT_EQ(ulong, kz->hash, zz->hash);
	ASSERT_STREQ(kz->land, zz->land);
	ASSERT_TRUE(kstr_equals(kz, zz));
	ASSERT_EQ(int, kstr_compare(kz, zz), 0);
	return 0;
}

static int test_kstr_comparation_2() {
	const struct kstr *kz = kstr_fetch(tvm, "aaaaa", -1);
	const struct kstr *zz = kstr_fetch(tvm, "aaaaaa", 6);

	ASSERT_TRUE(kstr_equals(kz, kz));
	ASSERT_TRUE(kstr_equals(zz, zz));
	ASSERT_EQ(int, kstr_compare(kz, kz), 0);
	ASSERT_EQ(int, kstr_compare(zz, zz), 0);

	ASSERT_FALSE(kz == zz);
	ASSERT_NE(ulong, kz->hash, zz->hash);
	ASSERT_LT(int, kz->len, zz->len);
	ASSERT_STRNE(kz->land, zz->land);
	ASSERT_FALSE(kstr_equals(kz, zz));
	ASSERT_LT(int, kstr_compare(kz, zz), 0);

	kz = kstr_fetch(tvm, "aaaaab", -1);
	zz = kstr_fetch(tvm, "aaaaaa", -1);

	ASSERT_NE(ulong, kz->hash, zz->hash);
	ASSERT_EQ(int, kz->len, zz->len);
	ASSERT_STRNE(kz->land, zz->land);
	ASSERT_FALSE(kstr_equals(kz, zz));
	ASSERT_GT(int, kstr_compare(kz, zz), 0);
	return 0;
}

static int test_kstr_kpool() {
	struct kstr *kz = kstr_fetch(tvm, "a", -1);
	ASSERT_STREQ(kz->land, "a");
	ASSERT_STREQ(kstr_fetch(tvm, "a", -1)->land, "a");
	ASSERT_TRUE(kz == kstr_fetch(tvm, "a", -1));

	kz = kstr_fetch(tvm, "aa", -1);
	ASSERT_STREQ(kz->land, "aa");
	ASSERT_STREQ(kstr_fetch(tvm, "aa", -1)->land, "aa");
	ASSERT_TRUE(kz == kstr_fetch(tvm, "aa", -1));
	ASSERT_FALSE(kz == kstr_fetch(tvm, "a", -1));
	return 0;
}

static int kstr_setup() {
	gc_active(tvm, GC_PAUSE);
	return 0;
}

static void kstr_teardown() {
	gc_active(tvm, GC_IDLE);
}

TEST_BEGIN_WITH(kstr_setup, kstr_teardown)
	TEST_ENTRY(kstr_creation, normal)
	TEST_ENTRY(kstr_comparation_1, posative)
	TEST_ENTRY(kstr_comparation_2, negative)
	TEST_ENTRY(kstr_kpool, normal)
TEST_END

