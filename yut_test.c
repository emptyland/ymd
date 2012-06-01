#include "yut.h"

static int test_string() {
	EXPECT_STREQ("123", "123");
	EXPECT_STRNE("123", "abc");
	EXPECT_STREQ("123", "abc");
	EXPECT_STRNE("123", "123");
	return 0;
}

static int test_condition() {
	EXPECT_TRUE(0);
	EXPECT_FALSE(1);
	EXPECT_NULL((void *)1);
	EXPECT_NOTNULL(NULL);
	return 0;
}

static int test_run() {
	int i = 0, j = 0;
	unsigned long long ll = 0xff00000000000000, lj = 0x7f00000000000000;
	EXPECT_EQ(uint, sizeof(long), 4);
	EXPECT_EQ(int, i, j + 1);

	EXPECT_GT(ularge, lj, ll);
	return 0;
}

static int test_foo() {
	EXPECT_EQ(int, -1, -1);
	EXPECT_EQ(long, -1, -1);
	EXPECT_EQ(uint, 1, 2);
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(run, normal)
	TEST_ENTRY(foo, normal)
	TEST_ENTRY(condition, normal)
	TEST_ENTRY(string, normal)
TEST_END

int main(int argc, char *argv[]) {
	return yut_run_all(argc, argv);
}
