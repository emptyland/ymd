#include "third_party/pcre/pcre.h"
#include "pcre_test.def"

static pcre *pattern(const char *str) {
	const char *err;
	int err_off;
	pcre *p = pcre_compile(str, PCRE_UTF8, &err, &err_off, NULL);
	if (!p) {
		printf("pcre_compile fatal: %s \"%s\"\n", err, str + err_off);
		return NULL;
	}
	return p;
}

static int test_sanity(void *p) {
	pcre *re = pattern("\\d+");
	ASSERT_NOTNULL(re);
	pcre_free(re);
	(void)p; return 0;
}

static int test_exec(void *p) {
	int rv, vec[6] = {0};
	pcre *re = pattern("\\d+");
	ASSERT_NOTNULL(re);
	rv = pcre_exec(re, NULL, "100", 3, 0, 0, vec, 3);
	ASSERT_EQ(int, 1, rv);
	ASSERT_EQ(int, 0, vec[0]);
	ASSERT_EQ(int, 3, vec[1]);

	rv = pcre_exec(re, NULL, "a100", 4, 0, 0, vec, 3);
	ASSERT_EQ(int, 1, rv);
	ASSERT_EQ(int, 1, vec[0]);
	ASSERT_EQ(int, 4, vec[1]);

	rv = pcre_exec(re, NULL, "a", 1, 0, 0, vec, 3);
	ASSERT_EQ(int, -1, rv);

	rv = pcre_exec(re, NULL, "100 200", 7, 0, 0, vec, 6);
	ASSERT_EQ(int, 1, rv);
	ASSERT_EQ(int, 0, vec[0]);
	ASSERT_EQ(int, 3, vec[1]);
	pcre_free(re);
	(void)p; return 0;
}

static int test_exec2(void *p) {
	const char *sub, **list;
	int rv, vec[9] = {0};
	pcre *re = pattern("\\d+");
	ASSERT_NOTNULL(re);

	sub = "100";
	rv = pcre_exec(re, NULL, sub, 3, 0, 0, vec, 9);
	rv = pcre_get_substring_list(sub, vec, rv, &list);
	ASSERT_STREQ(sub, list[0]);
	pcre_free_substring_list(list);

	sub = "200|100";
	rv = pcre_exec(re, NULL, sub, 7, 0, 0, vec, 9);
	rv = pcre_get_substring_list(sub, vec, rv, &list);
	ASSERT_STREQ("200", list[0]);
	pcre_free_substring_list(list);

	pcre_free(re);
	(void)p; return 0;
}

static int test_exec3(void *p) {
	const char *sub, **list;
	int rv, vec[9] = {0};
	pcre *re = pattern("pattern=(\\d+)\\.(\\w+)");
	ASSERT_NOTNULL(re);

	sub = "pattern=100.text";
	rv = pcre_exec(re, NULL, sub, strlen(sub), 0, 0, vec, 9);
	rv = pcre_get_substring_list(sub, vec, rv, &list);
	ASSERT_STREQ(sub, list[0]);
	ASSERT_STREQ("100", list[1]);
	ASSERT_STREQ("text", list[2]);
	pcre_free_substring_list(list);

	pcre_free(re);
	(void)p; return 0;
}

static int test_jit_exec(void *p) {
	const char *sub, **list;
	int rv, vec[9] = {0};
	pcre_jit_stack *js = pcre_jit_stack_alloc(1024, 4096);
	pcre *re = pattern("(\\d+)\\+(\\d+)");
	pcre_extra *ex = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &sub);

	sub = "100+99";
	rv = pcre_jit_exec(re, ex, sub, strlen(sub), 0, 0, vec, 9, js);
	rv = pcre_get_substring_list(sub, vec, rv, &list);
	ASSERT_STREQ(sub, list[0]);
	ASSERT_STREQ("100", list[1]);
	ASSERT_STREQ("99", list[2]);
	pcre_free_substring_list(list);

	pcre_jit_stack_free(js);
	pcre_free(re);
	(void)p; return 0;
}
