#include "print.h"
#include "flags.h"
#include "yut.h"
#include <sys/time.h>
#include <stdarg.h>
#include <assert.h>

static int yut_fault_log2(
	const char *file,
	int line,
	int asserted,
	const char *actual,
	const char *expected,
	const char *tag,
	va_list ap) {
	char fmt[1024];
	snprintf(fmt, sizeof(fmt),
			"${[!yellow][  FAILURE ]}$ %s:%d Failure\n"
			"Value of: %s\n"
			"  Actual: %s\n"
			"Expected: %s\n"
			"Which is: %s\n",
			file, line, actual, tag, expected, tag);
	ymd_vfprintf(stdout, fmt, ap);
	return asserted ? -1 : 1;
}

int yut_run_log1(
	const char *file,
	int line,
	int asserted,
	const char *actual,
	const char *expected,
	const char *condition) {
	ymd_printf("${[!yellow][  FAILURE ]}$ %s:%d Failure\n"
			"Value of: %s\n"
			"  Actual: %s\n"
			"Expected: %s\n",
	       file, line, condition, actual, expected);
	return asserted ? -1 : 1;
}

int yut_run_log2(
	const char *file,
	int line,
	int asserted,
	const char *actual,
	const char *expected,
	const char *tag,
	...) {
	va_list ap;
	va_start(ap, tag);
	yut_fault_log2(file, line, asserted, actual, expected, tag, ap);
	va_end(ap);
	return asserted ? -1 : 1;
}

int yut_run_logz(
	const char *file,
	int line,
	int asserted,
	const char *actual,
	const char *expected,
	const char *rhs,
	const char *lhs) {
	ymd_printf("${[!yellow][  FAILURE ]}$ %s:%d Failure\n"
			"Value of: %s\n"
			"  Actual: \"%s\"\n"
			"Expected: %s\n"
			"Which is: \"%s\"\n",
			file, line, actual, expected, rhs, lhs); 
	return asserted ? -1 : 1;
}

static const char *format_interval(
	const struct timeval *start,
    const struct timeval *jiffx,
	char buf[],
	size_t len) {
	unsigned long long jms = jiffx->tv_sec * 1000ULL + jiffx->tv_usec / 1000ULL,
	                   bms = start->tv_sec * 1000ULL + start->tv_usec / 1000ULL;
	snprintf(buf, len, "%02llu:%02llu.%03llu",
		    (jms - bms) / 1000ULL / 60ULL,
		    (jms - bms) / 1000ULL,
		    (jms - bms) % 1000ULL);
	return buf;
}

int yut_run_test(yut_case_t func, void *context, const char *name) {
	int err, rv;
	struct timeval jiffx, start;
	char itv[32];
	ymd_printf("${[!green][ RUN      ]}$ %s\n", name);
	rv = gettimeofday(&start, NULL);
	err = (*func)(context);
	rv = gettimeofday(&jiffx, NULL);
	if (err)
		ymd_printf("${[red][  FAILED  ]}$ %s (%s)\n", name,
				format_interval(&start, &jiffx, itv, sizeof(itv)));
	else
		ymd_printf("${[!green][       OK ]}$ %s (%s)\n", name,
				format_interval(&start, &jiffx, itv, sizeof(itv)));
	return err;
}

//--------------------------------------------------------------------------
// Time recording:
//--------------------------------------------------------------------------
static struct yut_time_count *top;

int yut_time_record(const char *file, int line, const char *id,
                    struct yut_time_count *x) {
	int rv;
	x->file = file;
	x->line = line;
	x->id   = id;
	rv = gettimeofday(&x->val, NULL);
	x->chain = top;
	top = x;
	return rv;
}

int yut_time_log1(const char *file, int line) {
	int rv;
	struct timeval jiffx;
	char itv[32];
	assert(top != NULL);
	rv = gettimeofday(&jiffx, NULL);
	ymd_printf("${[!green][   COST   ]}$ === %s:%d code: %s\n",
		   top->file, top->line, top->id);
	ymd_printf("${[!green][       -- ]}$ --- %s:%d cost: %s\n",
		   file, line,
		   format_interval(&top->val, &jiffx, itv, sizeof(itv)));
	return rv;
}

//-----------------------------------------------------------------------------
// Args Parsing:
//-----------------------------------------------------------------------------
struct filter {
	int negative; // Is Negatvie?
	const char *test_pattern;
	const char *case_pattern;
};

struct {
	int list;
	int repeated;
	int color;
	struct filter filter;
} yut_opt = { 0, 1, FLAG_AUTO, { 0, NULL, NULL, }, };

static int FlagYutFilter(const char *z, void *data);

const struct ymd_flag_entry yut_opt_entries[] = {
	{
		"color",
		"Colored test output.",
		&yut_opt.color,
		FlagTriBool,
	}, {
		"filter",
		"Test filter. \"*\" for any test; \"-\" for no test.",
		&yut_opt.filter,
		FlagYutFilter,
	}, {
		"repeated",
		"Number of repeated running test.",
		&yut_opt.repeated,
		FlagInt,
	}, {
		"list",
		"List all tests only.",
		&yut_opt.list,
		FlagBool,
	}, FLAG_END
};

// Filter: is any?
static YUT_INLINE int fany(const char *z) {
	return (!z || !z[0] || z[0] == '*');
}

static YUT_INLINE int fok(const char *pattern, const char *z) {
	return fany(pattern) || strcmp(pattern, z) == 0;
}

static YUT_INLINE
const char *fstr(const struct filter *filter, char *buf, size_t len) {
	snprintf(buf, len, "%s%s.%s",
			filter->negative ? "-" : "",
			filter->test_pattern ? filter->test_pattern : "*",
			filter->case_pattern ? filter->case_pattern : "*");
	return buf;
}

static int FlagYutFilter(const char *z, void *data) {
	struct filter *filter = data;
	const char *p = strchr(z, '=');
	if (!p)
		return -1;
	z = p + 1;
	if (z[0] == '-') {
		filter->negative = 1;
		++z;
	} else {
		filter->negative = 0;
	}
	p = strchr(z, '.');
	if (!p) {
		filter->test_pattern = strdup(z);
		return 0;
	}
	filter->test_pattern = strndup(z, p - z);
	filter->case_pattern = strdup(p + 1);
	return 0;
}

// All tests
extern const struct yut_case_def *yut_intl_test[];

static int yut_foreach_with() {
	int err = 0;
	const struct yut_case_def **x = NULL;
	for (x = yut_intl_test; *x != NULL; ++x) {
		const struct yut_case_def *test = *x;
		void *context = NULL;
		int i = 0;

		// Do filter check for test:
		if (yut_opt.filter.negative) {
			if ( fok(yut_opt.filter.test_pattern, test->name))
				continue;
		} else {
			if (!fok(yut_opt.filter.test_pattern, test->name))
				continue;
		}

		// Setup
		ymd_printf("${[!green][==========]}$ %s setup\n", test->name);
		// Run defined test
		for (i = 0; i < YUT_MAX_CASE; ++i) {
			char full_name[128];
			if (!test->caze[i].name || !test->caze[i].func)
				break;
			// Do filter check for test case:
			if (yut_opt.filter.negative) {
				if (fok(yut_opt.filter.test_pattern, test->name) &&
						fok(yut_opt.filter.case_pattern, test->caze[i].name))
					continue;
			} else {
				if (fok(yut_opt.filter.test_pattern, test->name) &&
						!fok(yut_opt.filter.case_pattern, test->caze[i].name))
					continue;
			}
			snprintf(full_name, sizeof(full_name), "%s.%s",
					test->name, test->caze[i].name);
			// Setup
			if (test->setup)
				context = (*test->setup)();
			if (yut_run_test(test->caze[i].func, context, full_name) < 0)
				++err;
			// Teardown
			if (test->teardown)
				(*test->teardown)(context);
		}
		// Test Finalize
		ymd_printf("${[!green][==========]}$ %s teardown\n\n", test->name);
	}
	return err;
}

int yut_list_all() {
	const struct yut_case_def **x = NULL;
	ymd_printf("Current all tests:\n");
	for (x = yut_intl_test; *x != NULL; ++x) {
		int i;
		const struct yut_case_def *test = *x;

		ymd_printf("${[blue]%s}$\n", test->name);
		for (i = 0; i < YUT_MAX_CASE; ++i) {
			if (!test->caze[i].name)
				break;
			ymd_printf("    ${[!yellow]%s}$\n", test->caze[i].name);
		}
	}
	return 0;
}

int yut_run_all(int argc, char *argv[]) {
	int i, err = 0;

	ymd_flags_parse(yut_opt_entries, NULL, &argc, &argv, 0);
	if (yut_opt.list) {
		yut_list_all();
		goto final;
	}
	if (yut_opt.filter.test_pattern ||
			yut_opt.filter.case_pattern) {
		char buf[128];
		ymd_printf("${[!yellow]Use filter: %s}$\n",
				fstr(&yut_opt.filter, buf, sizeof(buf)));
	}
	for (i = 0; i < yut_opt.repeated; ++i) {
		if (yut_opt.repeated > 1)
			ymd_printf("${[!yellow]Repeated test %d of %d ...}$\n",
					i + 1, yut_opt.repeated);
		err += yut_foreach_with();
	}

final:
	free((void*)yut_opt.filter.test_pattern);
	free((void*)yut_opt.filter.case_pattern);
	return err;
}

