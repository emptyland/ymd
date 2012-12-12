#include "print.h"
#include "yut.h"
#include <sys/time.h>
#include <stdarg.h>
#include <assert.h>

static int yut_fault_log2(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *expected,
	const char *actual,
	const char *tag,
	va_list ap) {
	const char *s = asserted ? "Assertion" : "Expection";
	char fmt[1024];
	snprintf(fmt, sizeof(fmt),
		 "${[yellow][   INFO   ] %s:%d %s Failed!\n"
		 "[       -- ] Notice: (%s) %s (%s)\n"
		 "[       -- ] %s aka. %s\n"
		 "[       -- ] %s aka. %s}$\n",
		 file, line, s, expected, desc, actual, expected, tag,
		 actual, tag);
	return ymd_vfprintf(stdout, fmt, ap);
}

int yut_run_log1(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *condition) {
	const char *s = asserted ? "Assertion" : "Expection";
	ymd_printf("${[yellow][   INFO   ] %s:%d %s Failed!\n"
	       "[       -- ] Notice: (%s) %s}$\n",
	       file, line, s, condition, desc);
	return asserted ? -1 : 1;
}

int yut_run_log2(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *expected,
	const char *actual,
	const char *tag,
	...) {
	va_list ap;
	va_start(ap, tag);
	yut_fault_log2(file, line, asserted, desc, expected, actual, tag,
	               ap);
	va_end(ap);
	return asserted ? -1 : 1;
}

int yut_run_logz(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *expected,
	const char *actual,
	const char *lhs,
	const char *rhs) {
	const char *s = asserted ? "Assertion" : "Expection";
	ymd_printf("${[yellow][   INFO   ] %s:%d %s Failed!\n"
	       "[       -- ] Notice: (%s) %s (%s)\n"
	       "[       -- ] %s aka. %s\n"
	       "[       -- ] %s aka. %s}$\n",
	       file, line, s, expected, desc, actual, expected, lhs,
	       actual, rhs);
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
enum yut_cmd {
	YUT_TEST, // Run test.
	YUT_LIST, // List all test name.
	YUT_HELP, // Show usage.
};

struct filter {
	int negative; // Is Negatvie?
	const char *test_pattern;
	const char *case_pattern;
};

struct options {
	enum yut_cmd cmd; // Which action should do?
	struct filter filter;
	int repeated;
};

// Filter: is any?
static inline int fany(const char *z) {
	return (!z || !z[0] || z[0] == '*');
}

static inline int fok(const char *pattern, const char *z) {
	return fany(pattern) || strcmp(pattern, z) == 0;
}

static const char *prefix_filter   = "filter=";
static const char *prefix_repeated = "repeated=";
static const char *prefix_list     = "list";
static const char *prefix_help     = "help";

static int yut_parse_args(int argc, char *argv[], struct options *opt) {
	int i;

	memset(opt, 0, sizeof(*opt));
	opt->repeated = 1;
	for (i = 1; i < argc; ++i) {
		const char *argz = argv[i];
		if (strstr(argz, "--") != argz)
			continue;

		argz += 2; // Skip "--"
		if (strstr(argz, prefix_filter) == argz) {
			const char *p;
			argz += strlen(prefix_filter); // Skip "filter="
			if (!argz[0])
				continue;
			if (argz[0] == '-') {
				++argz;
				opt->filter.negative = 1;
			}
			// Split '.'
			if ((p = strchr(argz, '.')) == NULL) {
				opt->filter.test_pattern = strdup(argz);
			} else {
				opt->filter.test_pattern = strndup(argz, p - argz);
				opt->filter.case_pattern = strdup(p + 1);
			}
		} else if (strstr(argz, prefix_repeated) == argz) {
			argz += strlen(prefix_repeated);
			opt->repeated = atoi(argz);
			if (opt->repeated <= 0)
				opt->repeated = 1;
		} else if (strcmp(argz, prefix_list) == 0) {
			opt->cmd = YUT_LIST;
		} else if (strcmp(argz, prefix_help) == 0) {
			opt->cmd = YUT_HELP;
		}
	}
	return 0;
}


// All tests
extern const struct yut_case_def *yut_intl_test[];

static int yut_foreach_with(struct options *opt) {
	int err = 0;
	const struct yut_case_def **x = NULL;
	for (x = yut_intl_test; *x != NULL; ++x) {
		const struct yut_case_def *test = *x;
		void *context = NULL;
		int i = 0;

		// Do filter check for test:
		if (opt->filter.negative) {
			if ( fok(opt->filter.test_pattern, test->name))
				continue;
		} else {
			if (!fok(opt->filter.test_pattern, test->name))
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
			if (opt->filter.negative) {
				if (fok(opt->filter.test_pattern, test->name) &&
						fok(opt->filter.case_pattern, test->caze[i].name))
					continue;
			} else {
				if (fok(opt->filter.test_pattern, test->name) &&
						!fok(opt->filter.case_pattern, test->caze[i].name))
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
	struct options opt;

	yut_parse_args(argc, argv, &opt);
	switch (opt.cmd) {
	case YUT_LIST:
		yut_list_all();
		goto final;
	case YUT_HELP:
		//yut_usage();
		goto final;
	case YUT_TEST:
		break;
	}
	for (i = 0; i < opt.repeated; ++i) {
		if (opt.repeated > 1)
			ymd_printf("${[!yellow]Repeated test %d of %d ...\n",
					i + 1, opt.repeated);
		err += yut_foreach_with(&opt);
	}

final:
	free((void*)opt.filter.test_pattern);
	free((void*)opt.filter.case_pattern);
	return err;
}

