#include "encode.h"
#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

#define ASSERT_ATOL(x) \
	ASSERT_EQ(large, x, xtoll(#x, &ok)); \
	ASSERT_TRUE(ok)
static int test_atol_hex() {
	int ok;
	ASSERT_ATOL(0x1);
	ASSERT_ATOL(0x0);
	ASSERT_ATOL(0x000001);
	ASSERT_ATOL(0x7FEFEFEF00000000);
	ASSERT_ATOL(0x7fefefef00000000);
	ASSERT_ATOL(0x7FFFFFFFFFFFFFFF);
	ASSERT_ATOL(0x7FfFfFfFfFfFfFfF);
	ASSERT_ATOL(0xFFFFFFFFFFFFFFFF);
	ASSERT_ATOL(0xffffffffffffffff);
	ASSERT_ATOL(0x8000000000000000);
	ASSERT_ATOL(0x0123456789abcdef);
	ASSERT_ATOL(0x0123456789ABCDEF);
	return 0;
}
#undef ASSERT_ATOL
#define ASSERT_ATOL(x) \
	ASSERT_EQ(large, x##LL, dtoll(#x, &ok)); \
	ASSERT_TRUE(ok)
static int test_atol_dec() {
	int ok;
	ASSERT_ATOL(0);
	ASSERT_ATOL(-1);
	ASSERT_ATOL(-0);
	ASSERT_ATOL(9999999999999999);
	ASSERT_ATOL(123456789);
	return 0;
}
#undef ASSERT_ATOL
#define ASSERT_ESC(rhs, lhs) \
	ASSERT_EQ(int, stresc(rhs, &x), sizeof(lhs) - 1); \
	ASSERT_STREQ(x, lhs); \
	vm_free(x)
static int test_stresc() {
	char *x;
	ASSERT_ESC("\\\"", "\"");
	ASSERT_ESC("\\a", "\a");
	ASSERT_ESC("\\b", "\b");
	ASSERT_ESC("\\f", "\f");
	ASSERT_ESC("\\n", "\n");
	ASSERT_ESC("\\r", "\r");
	ASSERT_ESC("\\t", "\t");
	ASSERT_ESC("\\v", "\v");
	ASSERT_ESC("\\x00", "\x00");
	ASSERT_ESC("\\xaA", "\xaa");
	ASSERT_ESC("\\xBa", "\xba");
	ASSERT_ESC("\\u0000", "\x00\x00");
	ASSERT_ESC("\\uaFbD", "\xaf\xbd");
	ASSERT_ESC("\\u0123", "\x01\x23");
	ASSERT_ESC("\\u4567", "\x45\x67");

	ASSERT_ESC("Hello\\tWorld!\\n", "Hello\tWorld!\n");
	ASSERT_ESC("I am:\\t\\uccCC\\ucCcC\n",
	           "I am:\t\xcc\xcc\xcc\xcc\n");
	return 0;
}
#undef ASSERT_ESC
static int test_encode() {
	ASSERT_EQ(ularge, zigzag_encode(1), 2);
	ASSERT_EQ(ularge, zigzag_encode(4500), 9000);
	ASSERT_EQ(ularge, zigzag_encode(0), 0);
	ASSERT_EQ(ularge, zigzag_encode(-1), 3);
	ASSERT_EQ(ularge, zigzag_encode(-2), 5);
	ASSERT_EQ(ularge, zigzag_encode(100), 200);
	ASSERT_EQ(ularge, zigzag_encode(-7500), 15001);

	ASSERT_EQ(large, zigzag_decode(2), 1);
	ASSERT_EQ(large, zigzag_decode(9000), 4500);
	ASSERT_EQ(large, zigzag_decode(0), 0);
	ASSERT_EQ(large, zigzag_decode(3), -1);
	ASSERT_EQ(large, zigzag_decode(5), -2);
	ASSERT_EQ(large, zigzag_decode(200), 100);
	ASSERT_EQ(large, zigzag_decode(15001), -7500);
	return 0;
}
#define ASSERT_EN(n, len) \
	i = varint16_encode(n, partal); \
	ASSERT_EQ(int, i, len)
#define ASSERT_PARTAL1(arg0) \
	ASSERT_EQ(uint, partal[0], arg0)
#define ASSERT_PARTAL2(arg0, arg1) \
	ASSERT_EQ(uint, partal[0], arg0); \
	ASSERT_EQ(uint, partal[1], arg1)
#define ASSERT_PARTAL3(arg0, arg1, arg2) \
	ASSERT_EQ(uint, partal[0], arg0); \
	ASSERT_EQ(uint, partal[1], arg1); \
	ASSERT_EQ(uint, partal[2], arg2)
#define ASSERT_PARTAL4(arg0, arg1, arg2, arg3) \
	ASSERT_EQ(uint, partal[0], arg0); \
	ASSERT_EQ(uint, partal[1], arg1); \
	ASSERT_EQ(uint, partal[2], arg2); \
	ASSERT_EQ(uint, partal[3], arg3)
static int test_varint16() {
	unsigned short partal[MAX_VARINT16_LEN];
	int i;

	ASSERT_EN(1, 1);
	ASSERT_PARTAL1(2);

	ASSERT_EN(-1, 1);
	ASSERT_PARTAL1(3);

	ASSERT_EN(-2, 1);
	ASSERT_PARTAL1(5);

	ASSERT_EN(0x10000, 2);
	ASSERT_PARTAL2(2, 0);

	ASSERT_EN(0x10001, 2);
	ASSERT_PARTAL2(2, 2);

	ASSERT_EN(0x100010001, 3);
	ASSERT_PARTAL3(2, 2, 2);

	ASSERT_EN(0x1000100010001, 4);
	ASSERT_PARTAL4(2, 2, 2, 2);
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(atol_hex, normal)
	TEST_ENTRY(atol_dec, normal)
	TEST_ENTRY(stresc, normal)
	TEST_ENTRY(encode, normal)
	TEST_ENTRY(varint16, normal)
TEST_END
