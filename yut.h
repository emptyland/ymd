#ifndef TEST_YUT_H
#define TEST_YUT_H

#include "yut_type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

//--------------------------------------------------------------------------
// yut = [Y]amada [U]nit [T]est
//--------------------------------------------------------------------------

#define EQ_DO(to, l, r)  (to(l) == to(r))
#define EQ_DESC          "equal"
#define NE_DO(to, l, r)  (to(l) != to(r))
#define NE_DESC          "not equal"
#define GT_DO(to, l, r)  (to(l) > to(r))
#define GT_DESC          "greater then"
#define GE_DO(to, l, r)  (to(l) >= to(r))
#define GE_DESC          "greater or equal"
#define LT_DO(to, l, r)  (to(l) < to(r))
#define LT_DESC          "less"
#define LE_DO(to, l, r)  (to(l) <= to(r))
#define LE_DESC          "less or equal"

//--------------------------------------------------------------------------
// Integer binary comparation:
//--------------------------------------------------------------------------
#define ASSERT_BIN(action, type, expected, actual) \
if (!action##_DO(type##_cast, expected, actual)) \
	return yut_run_log2(__FILE__, __LINE__, 1, \
		action##_DESC, #expected, #actual, \
		type##_tag, expected, actual)

#define EXPECT_BIN(action, type, expected, actual) \
if (!action##_DO(type##_cast, expected, actual)) \
	yut_run_log2(__FILE__, __LINE__, 0, \
		action##_DESC, #expected, #actual, \
		type##_tag, expected, actual)

#define ASSERT_EQ(type, expected, actual) \
	ASSERT_BIN(EQ, type, expected, actual)
#define ASSERT_NE(type, expected, actual) \
	ASSERT_BIN(NE, type, expected, actual)
#define ASSERT_GT(type, expected, actual) \
	ASSERT_BIN(GT, type, expected, actual)
#define ASSERT_GE(type, expected, actual) \
	ASSERT_BIN(GE, type, expected, actual)
#define ASSERT_LT(type, expected, actual) \
	ASSERT_BIN(LT, type, expected, actual)
#define ASSERT_LE(type, expected, actual) \
	ASSERT_BIN(LE, type, expected, actual)

#define ASSERT_TRUE(condition) \
	if (!(condition)) \
		return yut_run_log1(__FILE__, __LINE__, 1, \
			"must be true(not equal 0)", \
			#condition)
#define ASSERT_FALSE(condition) \
	if (condition) \
		return yut_run_log1(__FILE__, __LINE__, 1, \
			"must be false(equal 0)", \
			#condition)
#define ASSERT_NULL(condition) \
	if ((condition) != NULL) \
		return yut_run_log1(__FILE__, __LINE__, 1, \
			"must be NULL", #condition)
#define ASSERT_NOTNULL(condition) \
	if ((condition) == NULL) \
		return yut_run_log1(__FILE__, __LINE__, 1, \
			"must be NOT NULL", #condition)

#define EXPECT_EQ(type, expected, actual) \
	EXPECT_BIN(EQ, type, expected, actual)
#define EXPECT_NE(type, expected, actual) \
	EXPECT_BIN(NE, type, expected, actual)
#define EXPECT_GT(type, expected, actual) \
	EXPECT_BIN(GT, type, expected, actual)
#define EXPECT_GE(type, expected, actual) \
	EXPECT_BIN(GE, type, expected, actual)
#define EXPECT_LT(type, expected, actual) \
	EXPECT_BIN(LT, type, expected, actual)
#define EXPECT_LE(type, expected, actual) \
	EXPECT_BIN(LE, type, expected, actual)

//--------------------------------------------------------------------------
// Boolean check: c has no boolean type, it's `int` type.
//--------------------------------------------------------------------------
#define EXPECT_TRUE(condition) \
	if (!(condition)) \
		yut_run_log1(__FILE__, __LINE__, 0, \
			"should be true(not equal 0)", \
			#condition)
#define EXPECT_FALSE(condition) \
	if (condition) \
		yut_run_log1(__FILE__, __LINE__, 0, \
			"should be false(equal 0)", \
			#condition)
#define EXPECT_NULL(condition) \
	if ((condition) != NULL) \
		yut_run_log1(__FILE__, __LINE__, 0, \
			"should be NULL", #condition)
#define EXPECT_NOTNULL(condition) \
	if ((condition) == NULL) \
		yut_run_log1(__FILE__, __LINE__, 0, \
			"should be NOT NULL", #condition)

//--------------------------------------------------------------------------
// String check:
//--------------------------------------------------------------------------
#define ASSERT_STREQ(expected, actual) \
	if (strcmp(expected, actual)) \
		return yut_run_logz(__FILE__, __LINE__, 1, "equal", \
			#expected, #actual, \
			expected, actual)

#define ASSERT_STRNE(expected, actual) \
	if (strcmp(expected, actual) == 0) \
		return yut_run_logz(__FILE__, __LINE__, 1, \
			"not equal", \
			#expected, #actual, \
			expected, actual)

#define EXPECT_STREQ(expected, actual) \
	if (strcmp(expected, actual)) \
		yut_run_logz(__FILE__, __LINE__, 0, "equal", \
			#expected, #actual, \
			expected, actual)

#define EXPECT_STRNE(expected, actual) \
	if (strcmp(expected, actual) == 0) \
		yut_run_logz(__FILE__, __LINE__, 0, \
			"not equal", \
			#expected, #actual, \
			expected, actual)

//--------------------------------------------------------------------------
// Test reporting functions:
//--------------------------------------------------------------------------
int yut_run_log1(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *condition);

int yut_run_log2(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *expected,
	const char *actual,
	const char *tag,
	...);

int yut_run_logz(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *expected_expr,
	const char *actual_expr,
	const char *expected,
	const char *actual);

//--------------------------------------------------------------------------
// Time recording:
//--------------------------------------------------------------------------
struct yut_time_count {
	struct yut_time_count *chain;
	const char *id;
	const char *file;
	int line;
	struct timeval val;
};

#define TIME_RECORD_BEGIN(id) { \
	struct yut_time_count __intl_counter_##id##_xx; \
	yut_time_record(__FILE__, __LINE__, #id, &__intl_counter_##id##_xx);

#define TIME_RECORD_END \
	yut_time_log1(__FILE__, __LINE__); \
}

int yut_time_record(const char *file, int line, const char *id,
                    struct yut_time_count *ctx);
int yut_time_log1(const char *file, int line);

//--------------------------------------------------------------------------
// xterm color defines:
//--------------------------------------------------------------------------
#define YUT_COLOR_RED    "1;31"
#define YUT_COLOR_GREEN  "1;32"
#define YUT_COLOR_YELLOW "1;33"
#define YUT_COLOR_BLUE   "1;34"
#define YUT_COLOR_PURPLE "1;35"
#define YUT_COLOR_AZURE  "1;36"

#define yut_color(color)   YUT_COLOR_##color
#define yut_colored(color) "\033[" YUT_COLOR_##color "m"
#define yut_colorless()    "\033[0m"

#define yut_paint_z(color) printf(yut_colored(color))
#define yut_final_z() printf(yut_colorless())


//--------------------------------------------------------------------------
// Test case enter:
//--------------------------------------------------------------------------
typedef int (*yut_routine_t)();
int yut_run_test(yut_routine_t fn, const char *name);
int yut_run_all(int argc, char *argv[]);

#define RUN_TEST(fn, postfix) \
	if (yut_run_test(test_##fn, #fn"."#postfix) < 0) \
		return -1

struct yut_ent {
	yut_routine_t fn;
	const char *name;
};

struct yut_env {
	int (*setup)();
	void (*teardown)();
};

#define TEST_BEGIN \
struct yut_env yut_intl_env = { NULL, NULL, }; \
struct yut_ent yut_intl_test[] = {

#define TEST_BEGIN_WITH(setup, teardown) \
struct yut_env yut_intl_env = { setup, teardown, }; \
struct yut_ent yut_intl_test[] = {

#define TEST_END \
	{ .fn = NULL, .name = NULL, }, \
};

#define TEST_ENTRY(func, postfix) { \
	.fn = test_##func, \
	.name = #func"."#postfix, \
},

struct ymd_mach;
extern struct ymd_mach *tvm;

#endif //TEST_YUT_H
