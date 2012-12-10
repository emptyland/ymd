#include "yut_rand.h"
#include "testing/yut_test.def"

static int test_sanity(void *p) {
	(void)p;
	printf("It's sanity!\n");
	return 0;
}

static int test_rand(void *p) {
	int i = 10000;
	(void)p;
	RAND_BEGIN(NORMAL)
		while (i--) {
			unsigned long long rv;
			ASSERT_LE(ularge, RAND_RANGE(ularge, 0, 10000), 10000);
			rv = RAND_RANGE(ularge, 1, 2);
			ASSERT_LE(ularge, rv, 2);
			ASSERT_GE(ularge, rv, 1);
		}
		i = 16;
		while (i--) {
			printf("[ RAND ] %s\n", RANDz());
		}
	RAND_END
	return 0;
}

static int test_string(void *p) {
	(void)p;
	EXPECT_STREQ("123", "123");
	EXPECT_STRNE("123", "abc");
	EXPECT_STREQ("123", "abc");
	EXPECT_STRNE("123", "123");
	return 0;
}

static int test_condition(void *p) {
	(void)p;
	EXPECT_TRUE(0);
	EXPECT_FALSE(1);
	EXPECT_NULL((void *)1);
	EXPECT_NOTNULL(NULL);
	return 0;
}

static int test_run(void *p) {
	int i = 0, j = 0;
	unsigned long long ll = 0xff00000000000000, lj = 0x7f00000000000000;
	(void)p;
	EXPECT_EQ(uint, sizeof(long), 4);
	EXPECT_EQ(int, i, j + 1);

	EXPECT_GT(ularge, lj, ll);
	return 0;
}

static int test_foo(void *p) {
	(void)p;
	EXPECT_EQ(int, -1, -1);
	EXPECT_EQ(long, -1, -1);
	EXPECT_EQ(uint, 1, 2);
	return 0;
}

