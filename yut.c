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

int yut_run_test(yut_routine_t fn, const char *name) {
	int err, rv;
	struct timeval jiffx, start;
	char itv[32];
	printf(yut_colored(GREEN)"[======]"yut_colorless()
	       " Test "yut_colored(PURPLE)"%s"yut_colorless()" setup\n",
		   name);
	printf(yut_colored(GREEN)"[ RUN  ]"yut_colorless()
	       " Running ...\n");
	rv = gettimeofday(&start, NULL);
	err = (*fn)();
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


extern struct yut_ent yut_intl_test[];
extern struct yut_env yut_intl_env;

int yut_run_all(int argc, char *argv[]) {
	struct yut_ent *i = yut_intl_test;
	(void)argc;
	(void)argv;
	while (i->fn) {
		if (yut_intl_env.setup)
			(*yut_intl_env.setup)();
		int err = yut_run_test(i->fn, i->name);
		if (yut_intl_env.teardown)
			(*yut_intl_env.teardown)();
		if (err)
			return err;
		++i;
	}
	return 0;
}
