#include "encoding.h"
#include "yut_rand.h"
#include "testing/encoding_test.def"

#define ASSERT_ATOL(x) \
	ASSERT_EQ(large, x, xtoll(#x, &ok)); \
	ASSERT_TRUE(ok)
static int test_atol_hex (void *p) {
	int ok;
	(void)p;
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
static int test_atol_dec (void *p) {
	int ok;
	(void)p;
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
	free(x)
static int test_stresc (void *p) {
	char *x;
	(void)p;
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
static int test_encode (void *p) {
	(void)p;
	ASSERT_EQ(ularge, zigzag64encode(1), 2);
	ASSERT_EQ(ularge, zigzag64encode(4500), 9000);
	ASSERT_EQ(ularge, zigzag64encode(0), 0);
	ASSERT_EQ(ularge, zigzag64encode(-1), 3);
	ASSERT_EQ(ularge, zigzag64encode(-2), 5);
	ASSERT_EQ(ularge, zigzag64encode(100), 200);
	ASSERT_EQ(ularge, zigzag64encode(-7500), 15001);

	ASSERT_EQ(large, zigzag64decode(2), 1);
	ASSERT_EQ(large, zigzag64decode(9000), 4500);
	ASSERT_EQ(large, zigzag64decode(0), 0);
	ASSERT_EQ(large, zigzag64decode(3), -1);
	ASSERT_EQ(large, zigzag64decode(5), -2);
	ASSERT_EQ(large, zigzag64decode(200), 100);
	ASSERT_EQ(large, zigzag64decode(15001), -7500);
	return 0;
}

static int test_varint64 (void *p) {
	ymd_byte_t bin[MAX_VARINT64_LEN];
	int i = sint64encode(0, bin);
	size_t k = 0;
	(void)p;
	ASSERT_EQ(int, i, 1);
	ASSERT_EQ(uint, bin[0], 0);
	ASSERT_EQ(large, sint64decode(bin, &k), 0);
	ASSERT_EQ(int, i, k);

	i = sint64encode(-1, bin);
	ASSERT_EQ(int, i, 1);
	ASSERT_EQ(uint, bin[0], 3);
	ASSERT_EQ(large, sint64decode(bin, &k), -1);
	ASSERT_EQ(int, i, k);

	i = sint64encode(0x3F, bin);
	ASSERT_EQ(int, i, 1);
	ASSERT_EQ(uint, bin[0], 0x3F << 1);
	ASSERT_EQ(large, sint64decode(bin, &k), 0x3F);
	ASSERT_EQ(int, i, k);

	i = sint64encode(0x40, bin);
	ASSERT_EQ(int, i, 2);
	ASSERT_EQ(uint, bin[0], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[1], 0);
	ASSERT_EQ(large, sint64decode(bin, &k), 0x40);
	ASSERT_EQ(int, i, k);

	i = sint64encode(0x2040, bin);
	ASSERT_EQ(int, i, 3);
	ASSERT_EQ(uint, bin[0], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[1], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[2], 0);
	ASSERT_EQ(large, sint64decode(bin, &k), 0x2040);
	ASSERT_EQ(int, i, k);

	i = sint64encode(0x102040, bin);
	ASSERT_EQ(int, i, 4);
	ASSERT_EQ(uint, bin[0], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[1], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[2], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[3], 0);
	ASSERT_EQ(large, sint64decode(bin, &k), 0x102040);
	ASSERT_EQ(int, i, k);

	i = sint64encode(0x4000000000000000LL, bin);
	ASSERT_EQ(int, i, 10);
	ASSERT_EQ(uint, bin[0], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[9], 0);
	ASSERT_EQ(large, sint64decode(bin, &k), 0x4000000000000000LL);
	ASSERT_EQ(int, i, k);
	return 0;
}

static int test_varint32 (void *p) {
	ymd_byte_t bin[MAX_VARINT32_LEN];
	int i = uint32encode(0, bin);
	size_t k = 0;
	(void)p;
	ASSERT_EQ(int, i, 1);
	ASSERT_EQ(uint, bin[0], 0);
	ASSERT_EQ(int, uint32decode(bin, &k), 0U);
	ASSERT_EQ(ulong, i, k);

	i = uint32encode(0x80, bin);
	ASSERT_EQ(int, i, 2);
	ASSERT_EQ(uint, bin[0], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[1], 0);
	ASSERT_EQ(int, uint32decode(bin, &k), 0x80U);
	ASSERT_EQ(ulong, i, k);

	i = uint32encode(0x10204080U, bin);
	ASSERT_EQ(int, i, 5);
	ASSERT_EQ(uint, bin[0], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[1], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[2], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[3], 0x80 | 0x1);
	ASSERT_EQ(uint, bin[4], 0);
	ASSERT_EQ(int, uint32decode(bin, &k), 0x10204080U);
	ASSERT_EQ(ulong, i, k);
	return 0;
}

