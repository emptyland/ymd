#include "flags.h"
#include "testing/flags_test.def"

struct {
	int b;
	int i;
	char z[MAX_FLAG_STRING_LEN];
	char *ps;
	int color;
} test_opt = { 1, 0, "", NULL, 0, };

const struct ymd_flag_entry test_entries[] = {
	{
		"b",
		"Bool value.",
		&test_opt.b,
		FlagBool,
	}, {
		"i",
		"Int value.",
		&test_opt.i,
		FlagInt,
	}, {
		"z",
		"Static string value.",
		test_opt.z,
		FlagString,
	}, {
		"ps",
		"Malloced string value.",
		&test_opt.ps,
		FlagStringMalloced,
	}, {
		"color",
		"Color output option.",
		&test_opt.color,
		FlagTriBool,
	}, FLAG_END
};

static void *setup() {
	memset(&test_opt, 0, sizeof(test_opt));
	return &test_opt;
}

static void teardown(void *p) {
	(void)p;
	ymd_flags_free(test_opt.ps);
}

static int test_sanity(void *p) {
	char *cmdline[] = {
		"none",
		"--b=yes",
		"--i=65535",
		"--ps=foo.bar",
		"--color=auto",
		NULL,
	};
	char **argv = (char **)cmdline;
	int argc = sizeof(cmdline)/sizeof(cmdline[0]) - 1;
	(void)p;

	ymd_flags_parse(test_entries, NULL, &argc, &argv, 0);
	ASSERT_TRUE(test_opt.b);
	ASSERT_EQ(int, 65535, test_opt.i);
	ASSERT_STREQ("foo.bar", test_opt.ps);
	ASSERT_EQ(int, FLAG_AUTO, test_opt.color);
	return 0;
}
