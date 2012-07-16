#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut.h"

static int test_kstr_creation() {
	const struct kstr *kz = kstr_new(tvm, "abcdef", 6);
	ASSERT_NOTNULL(kz);
	ASSERT_EQ(int, kz->type, T_KSTR);
	ASSERT_STREQ(kz->land, "abcdef");
	ASSERT_EQ(int, kz->len, 6);

	kz = kstr_new(tvm, "abcdef", 3);
	ASSERT_STREQ(kz->land, "abc");
	ASSERT_EQ(int, kz->len, 3);

	kz = kstr_new(tvm, "abcdef", 0);
	ASSERT_STREQ(kz->land, "");
	ASSERT_EQ(int, kz->len, 0);

	kz = kstr_new(tvm, "abcdefgh", -1);
	ASSERT_STREQ(kz->land, "abcdefgh");
	ASSERT_EQ(int, kz->len, 8);
	return 0;
}

static int test_kstr_comparation_1() {
	const struct kstr *kz = kstr_new(tvm, "aaaaaa", -1);
	const struct kstr *zz = kstr_new(tvm, "aaaaaa", 6);

	ASSERT_FALSE(kz == zz);
	ASSERT_EQ(ulong, kz->hash, zz->hash);
	ASSERT_STREQ(kz->land, zz->land);
	ASSERT_TRUE(kstr_equals(kz, zz));
	ASSERT_EQ(int, kstr_compare(kz, zz), 0);

	kz = kstr_new(tvm, "", 0);
	zz = kstr_new(tvm, "", 0);

	ASSERT_FALSE(kz == zz);
	ASSERT_EQ(ulong, kz->hash, zz->hash);
	ASSERT_STREQ(kz->land, zz->land);
	ASSERT_TRUE(kstr_equals(kz, zz));
	ASSERT_EQ(int, kstr_compare(kz, zz), 0);
	return 0;
}

static int test_kstr_comparation_2() {
	const struct kstr *kz = kstr_new(tvm, "aaaaa", -1);
	const struct kstr *zz = kstr_new(tvm, "aaaaaa", 6);

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

	kz = kstr_new(tvm, "aaaaab", -1);
	zz = kstr_new(tvm, "aaaaaa", -1);

	ASSERT_NE(ulong, kz->hash, zz->hash);
	ASSERT_EQ(int, kz->len, zz->len);
	ASSERT_STRNE(kz->land, zz->land);
	ASSERT_FALSE(kstr_equals(kz, zz));
	ASSERT_GT(int, kstr_compare(kz, zz), 0);
	return 0;
}

static int test_kstr_kpool() {
	struct kstr *kz = ymd_kstr(tvm, "a", -1);
	ASSERT_STREQ(kz->land, "a");
	ASSERT_STREQ(ymd_kstr(tvm, "a", -1)->land, "a");
	ASSERT_TRUE(kz == ymd_kstr(tvm, "a", -1));

	kz = ymd_kstr(tvm, "aa", -1);
	ASSERT_STREQ(kz->land, "aa");
	ASSERT_STREQ(ymd_kstr(tvm, "aa", -1)->land, "aa");
	ASSERT_TRUE(kz == ymd_kstr(tvm, "aa", -1));
	ASSERT_FALSE(kz == ymd_kstr(tvm, "a", -1));
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(kstr_creation, normal)
	TEST_ENTRY(kstr_comparation_1, posative)
	TEST_ENTRY(kstr_comparation_2, negative)
	TEST_ENTRY(kstr_kpool, normal)
TEST_END

