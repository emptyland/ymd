#include "yut.h"
#include <stdarg.h>
#include <time.h>

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
	snprintf(fmt, sizeof(fmt), yut_color_begin(YELLOW) 
		 "[ INFO ] %s:%d %s Failed!\n"
		 "[   -- ] Notice: (%s) %s (%s)\n"
		 "[   -- ] %s aka. %s\n"
		 "[   -- ] %s aka. %s\n" yut_color_end(),
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
	printf(yut_color_begin(YELLOW) 
	       "[ INFO ] %s:%d %s Failed!\n"
	       "[   -- ] Notice: (%s) %s\n" yut_color_end(),
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
	printf(yut_color_begin(YELLOW) 
	       "[ INFO ] %s:%d %s Failed!\n"
	       "[   -- ] Notice: (%s) %s (%s)\n"
	       "[   -- ] %s aka. %s\n"
	       "[   -- ] %s aka. %s\n" yut_color_end(),
	       file, line, s, expected, desc, actual, expected, lhs,
	       actual, rhs);
	return asserted ? -1 : 1;
}

int yut_run_test(yut_routine_t fn, const char *name) {
	int err;
	clock_t jiff;
	printf(yut_color_begin(GREEN)"[======]"yut_color_end()
	       " Test %s startup\n", name);
	printf(yut_color_begin(GREEN)"[ RUN  ]"yut_color_end()
	       " Test running ...\n");
	jiff = clock();
	err = (*fn)();
	jiff = clock() - jiff;
	if (err)
		printf(yut_color_begin(RED)"[FALIED]"yut_color_end()
		       " Run test error\n");
	else
		printf(yut_color_begin(GREEN)"[   OK ]"yut_color_end()
		       " Run test ok\n");
	printf(yut_color_begin(GREEN)"[------]"yut_color_end()
	       " Test running final cast: %ld\n", jiff);
	return err;
}

extern struct yut_ent yut_intl_test[];

int yut_run_all(int argc, char *argv[]) {
	struct yut_ent *i = yut_intl_test;
	(void)argc;
	(void)argv;
	while (i->fn) {
		int err = yut_run_test(i->fn, i->name);
		if (err)
			return err;
		++i;
	}
	return 0;
}
