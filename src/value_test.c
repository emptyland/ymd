#include "core.h"
#include "yut_rand.h"
#include "testing/value_test.def"

static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, +1);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, -1);
	ymd_final(vm);
}

static int test_sanity(struct ymd_mach *vm) {
	(void)vm;
	struct variable var;
	setv_nil(&var);
	ASSERT_EQ(int, T_NIL, var.tt);
	ASSERT_EQ(large, 0, var.u.i);

	setv_int(&var, 1000);
	ASSERT_EQ(int, T_INT, var.tt);
	ASSERT_EQ(large, 1000, var.u.i);

	setv_bool(&var, 1);
	ASSERT_EQ(int, T_BOOL, var.tt);
	ASSERT_TRUE(var.u.i);

	setv_float(&var, 3.1415);
	ASSERT_EQ(int, T_FLOAT, var.tt);
	ASSERT_TRUE(3.1415 == var.u.f);
	return 0;
}

static int test_kstr_creation(struct ymd_mach *vm) {
	const struct kstr *kz = kstr_fetch(vm, "abcdef", 6);
	ASSERT_EQ(int, kz->marked, vm->gc.white);
	ASSERT_NOTNULL(kz);
	ASSERT_EQ(int, kz->type, T_KSTR);
	ASSERT_STREQ(kz->land, "abcdef");
	ASSERT_EQ(int, kz->len, 6);

	kz = kstr_fetch(vm, "abcdef", 3);
	ASSERT_EQ(int, kz->marked, vm->gc.white);
	ASSERT_STREQ(kz->land, "abc");
	ASSERT_EQ(int, kz->len, 3);

	kz = kstr_fetch(vm, "abcdef", 0);
	ASSERT_EQ(int, kz->marked, vm->gc.white);
	ASSERT_STREQ(kz->land, "");
	ASSERT_EQ(int, kz->len, 0);

	kz = kstr_fetch(vm, "abcdefgh", -1);
	ASSERT_EQ(int, kz->marked, vm->gc.white);
	ASSERT_STREQ(kz->land, "abcdefgh");
	ASSERT_EQ(int, kz->len, 8);
	return 0;
}

static int test_kstr_comparation_1(struct ymd_mach *vm) {
	const struct kstr *kz = kstr_fetch(vm, "aaaaaa", -1);
	const struct kstr *zz = kstr_fetch(vm, "aaaaaa", 6);

	ASSERT_TRUE(kz == zz);
	ASSERT_EQ(ulong, kz->hash, zz->hash);
	ASSERT_STREQ(kz->land, zz->land);
	ASSERT_TRUE(kstr_equals(kz, zz));
	ASSERT_EQ(int, kstr_compare(kz, zz), 0);

	kz = kstr_fetch(vm, "", 0);
	ASSERT_EQ(int, kz->marked, vm->gc.white);
	zz = kstr_fetch(vm, "", 0);
	ASSERT_EQ(int, zz->marked, vm->gc.white);

	ASSERT_TRUE(kz == zz);
	ASSERT_EQ(ulong, kz->hash, zz->hash);
	ASSERT_STREQ(kz->land, zz->land);
	ASSERT_TRUE(kstr_equals(kz, zz));
	ASSERT_EQ(int, kstr_compare(kz, zz), 0);
	return 0;
}

static int test_kstr_comparation_2(struct ymd_mach *vm) {
	const struct kstr *kz = kstr_fetch(vm, "aaaaa", -1);
	const struct kstr *zz = kstr_fetch(vm, "aaaaaa", 6);

	ASSERT_EQ(int, kz->marked, vm->gc.white);
	ASSERT_EQ(int, zz->marked, vm->gc.white);

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

	kz = kstr_fetch(vm, "aaaaab", -1);
	zz = kstr_fetch(vm, "aaaaaa", -1);

	ASSERT_NE(ulong, kz->hash, zz->hash);
	ASSERT_EQ(int, kz->len, zz->len);
	ASSERT_STRNE(kz->land, zz->land);
	ASSERT_FALSE(kstr_equals(kz, zz));
	ASSERT_GT(int, kstr_compare(kz, zz), 0);
	return 0;
}

static int test_kstr_kpool(struct ymd_mach *vm) {
	struct kstr *kz = kstr_fetch(vm, "a", -1);
	ASSERT_STREQ(kz->land, "a");
	ASSERT_STREQ(kstr_fetch(vm, "a", -1)->land, "a");
	ASSERT_TRUE(kz == kstr_fetch(vm, "a", -1));

	kz = kstr_fetch(vm, "aa", -1);
	ASSERT_STREQ(kz->land, "aa");
	ASSERT_STREQ(kstr_fetch(vm, "aa", -1)->land, "aa");
	ASSERT_TRUE(kz == kstr_fetch(vm, "aa", -1));
	ASSERT_FALSE(kz == kstr_fetch(vm, "a", -1));
	return 0;
}

