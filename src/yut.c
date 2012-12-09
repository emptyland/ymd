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
	snprintf(fmt, sizeof(fmt), yut_colored(YELLOW) 
		 "[ INFO ] %s:%d %s Failed!\n"
		 "[   -- ] Notice: (%s) %s (%s)\n"
		 "[   -- ] %s aka. %s\n"
		 "[   -- ] %s aka. %s\n" yut_colorless(),
		 file, line, s, expected, desc, actual, expected, tag,
		 actual, tag);
	return vprintf(fmt, ap);
}

int yut_run_log1(
	const char *file,
	int line,
	int asserted,
	const char *desc,
	const char *condition) {
	const char *s = asserted ? "Assertion" : "Expection";
	printf(yut_colored(YELLOW) 
	       "[ INFO ] %s:%d %s Failed!\n"
	       "[   -- ] Notice: (%s) %s\n" yut_colorless(),
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
	printf(yut_colored(YELLOW) 
	       "[ INFO ] %s:%d %s Failed!\n"
	       "[   -- ] Notice: (%s) %s (%s)\n"
	       "[   -- ] %s aka. %s\n"
	       "[   -- ] %s aka. %s\n" yut_colorless(),
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
	printf(yut_colored(GREEN)"[======]"yut_colorless()
	       " Test "yut_colored(PURPLE)"%s"yut_colorless()" setup\n",
		   name);
	printf(yut_colored(GREEN)"[ RUN  ]"yut_colorless()
	       " Running ...\n");
	rv = gettimeofday(&start, NULL);
	err = (*func)(context);
	rv = gettimeofday(&jiffx, NULL);
	if (err)
		printf(yut_colored(RED)"[FALIED]"yut_colorless()
		       " Run test error\n");
	else
		printf(yut_colored(GREEN)"[   OK ]"yut_colorless()
		       " Passed!\n");
	printf(yut_colored(GREEN)"[------]"yut_colorless()
	       " Test teardown, cast: %s\n",
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
	printf(yut_colored(GREEN)"[ COST ]"yut_colorless()
	       " === %s:%d code: %s\n",
		   top->file, top->line, top->id);
	printf(yut_colored(GREEN)"[   -- ]"yut_colorless()
	       " --- %s:%d cost: %s\n",
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

struct options {
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
		}
	}
	return 0;
}


// All tests
extern const struct yut_case_def *yut_intl_test[];

static int yut_foreach_with(struct options *opt) {
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
		printf(yut_colored(GREEN)"[======] "yut_colorless()
				"%s setup\n", test->name);
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
			yut_run_test(test->caze[i].func, context, full_name);
			// Teardown
			if (test->teardown)
				(*test->teardown)(context);
		}
		// Test Finalize
		printf(yut_colored(GREEN)"[======] "yut_colorless()
				"%s teardown\n\n", test->name);
	}
	return 0;
}

int yut_run_all(int argc, char *argv[]) {
	int i;
	struct options opt;

	yut_parse_args(argc, argv, &opt);
	for (i = 0; i < opt.repeated; ++i)
		yut_foreach_with(&opt);

	free((void*)opt.filter.test_pattern);
	free((void*)opt.filter.case_pattern);
	return 0;
}

