#include "zstream.h"
#include "yut.h"

static int test_zos_init () {
	struct zostream os = ZOS_INIT;
	ASSERT_EQ(int, os.last, 0);
	ASSERT_EQ(int, os.max,  MAX_STATIC_LEN);
	ASSERT_NULL(os.buf);
	return 0;
}

static int test_zos_reserved () {
	struct zostream os = ZOS_INIT;
	zos_reserved(&os, MAX_STATIC_LEN);
	ASSERT_NULL(os.buf);
	ASSERT_EQ(int, os.last, 0);
	ASSERT_EQ(int, os.max, MAX_STATIC_LEN);
	zos_final(&os);

	zos_reserved(&os, MAX_STATIC_LEN + 1);
	ASSERT_NOTNULL(os.buf);
	ASSERT_EQ(int, os.max, MAX_STATIC_LEN * 2);
	zos_final(&os);
	return 0;
}

static int test_zos_append () {
	struct zostream os = ZOS_INIT;
	ymd_u32_t  k = 0x0feedbadU;
	ymd_u32_t k1 = 0xccccccccU;
	ymd_u32_t k2 = 0xbadbadeeU;
	int n = MAX_STATIC_LEN / sizeof(k), i = n, n1;
	while (i--) zos_append(&os, &k, sizeof(k));
	ASSERT_EQ(int, os.max, MAX_STATIC_LEN);
	ASSERT_EQ(int, os.last, os.max);
	ASSERT_EQ(int, zos_remain(&os), 0);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[0], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[1], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n - 1], k);
	ASSERT_EQ(ulong, zos_last(&os) - zos_buf(&os), MAX_STATIC_LEN);

	zos_append(&os, &k1, sizeof(k1));
	ASSERT_EQ(int, os.max, MAX_STATIC_LEN * 2);
	ASSERT_EQ(int, os.last, MAX_STATIC_LEN + sizeof(k1));
	ASSERT_FALSE(zos_buf(&os) == os.kbuf);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[0], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[1], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n - 1], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n], k1);

	i = zos_remain(&os) / sizeof(k);
	n1 = n + 1 + i;
	while (i--) zos_append(&os, &k1, sizeof(k1));
	zos_append(&os, &k2, sizeof(k2));
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[0], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[1], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n - 1], k);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n], k1);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n1 - 1], k1);
	ASSERT_EQ(uint, ((ymd_u32_t*)zos_buf(&os))[n1], k2);
	zos_final(&os);
	return 0;
}

static int test_zis_init () {
	ymd_byte_t z[8] = {0};
	struct zistream is = ZIS_INIT(NULL, z, sizeof(z));
	ASSERT_EQ(int, is.last, 0);
	ASSERT_EQ(int, is.max, 8);
	ASSERT_NULL(is.err);
	ASSERT_TRUE(is.buf == z);
	ASSERT_NULL(is.l);
	return 0;
}

static int test_zis_fetch () {
	ymd_byte_t z[] = {
		0xef, 0xcd, 0xab, 0x12,
		0xdf, 0xba, 0xdf, 0xba,
		0x01, 0x02, 0x03, 0x04,
		0xcc, 0xcc, 0xcc, 0xcc,
	};
	ymd_u32_t k = 0;
	struct zistream is = ZIS_INIT(NULL, z, sizeof(z));
	zis_fetch(&is, &k, sizeof(k));
	ASSERT_EQ(uint, k, 0x12abcdefU);
	zis_fetch(&is, &k, sizeof(k));
	ASSERT_EQ(uint, k, 0xbadfbadfU);
	zis_fetch(&is, &k, sizeof(k));
	ASSERT_EQ(uint, k, 0x04030201U);
	zis_fetch(&is, &k, sizeof(k));
	ASSERT_EQ(uint, k, 0xccccccccU);
	return 0;
}

static int test_zss_encoding () {
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(NULL, NULL, 0);

	zos_u32(&os, 0x10204080U);
	zos_u64(&os, 0x80ULL);
	zos_i64(&os, -1LL);
	zos_u32(&os, 0U);
	zos_u64(&os, 0x80000000ULL);

	is.buf = zos_buf(&os);
	is.max = os.last;

	ASSERT_EQ(uint,   0x10204080U, zis_u32(&is));
	ASSERT_EQ(ularge, 0x80ULL,     zis_u64(&is));
	ASSERT_EQ(large,  -1LL,        zis_i64(&is));
	ASSERT_EQ(uint,   0U,          zis_u32(&is));
	ASSERT_EQ(ularge, 0x80000000U, zis_u64(&is));
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(zos_init, normal)
	TEST_ENTRY(zos_reserved, normal)
	TEST_ENTRY(zos_append, normal)
	TEST_ENTRY(zis_init, normal)
	TEST_ENTRY(zis_fetch, normal)
	TEST_ENTRY(zss_encoding, normal)
TEST_END
