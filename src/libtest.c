#include "tostring.h"
#include "core.h"
#include "libc.h"
#include "print.h"
#include <sys/time.h>
#include <setjmp.h>

// Test filter:
struct filter {
	int negative;
	const char *test_pattern;
	const char *case_pattern;
};

// Filter: is any?
static inline int fany(const char *z) {
	return (!z || !z[0] || z[0] == '*');
}

static inline int fok(const char *pattern, const char *z) {
	return fany(pattern) || strcmp(pattern, z) == 0;
}

static void fparse(const char *pattern, struct filter *filter) {
	const char *p = pattern;
	memset(filter, 0, sizeof(*filter));
	if (!pattern || !pattern[0])
		return;
	if (p[0] == '-') {
		filter->negative = 1;
		++p;
	}
	pattern = p;
	p = strchr(pattern, '.');
	if (!p) {
		filter->test_pattern = strdup(p);
		return;
	}
	filter->test_pattern = strndup(pattern, p - pattern);
	filter->case_pattern = strdup(p + 1);
}

static int ftest(const struct filter *filter, const char *test) {
	if (filter->negative) {
		if ( fok(filter->test_pattern, test))
			return 0;
	} else {
		if (!fok(filter->test_pattern, test))
			return 0;
	}
	return 1;
}

static int fcase(const struct filter *filter, const char *test,
		const char *caze) {
	if (filter->negative) {
		if (fok(filter->test_pattern, test) &&
				fok(filter->case_pattern, caze))
			return 0;
	} else {
		if (fok(filter->test_pattern, test) &&
				!fok(filter->case_pattern, caze))
			return 0;
	}
	return 1;
}

#define L struct ymd_context *l

struct yut_cookie {
	jmp_buf jpt;
};

static inline void yut_fault() {
	ymd_printf("${[red][  FAILED  ]}$ Test fail, stop all.\n");
}

static inline struct yut_cookie *yut_jpt(L, struct variable *self) {
	struct hmap *o = hmap_of(l, self);
	struct mand *cookie = mand_of(l, vm_mem(l->vm, o, "__cookie__"));
	return (struct yut_cookie *)cookie->land;
}

static inline void yut_raise(L) {
	struct yut_cookie *cookie = yut_jpt(l, ymd_argv_get(l, 0));
	longjmp(cookie->jpt, 1);
}

// [   INFO   ] Line:%d <%s> Assert fail.
static void yut_fail2(L, const char *op,
                      const struct variable *arg0,
                      const struct variable *arg1) {
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	struct zostream os0 = ZOS_INIT, os1 = ZOS_INIT;
	tostring(&os0, arg0);
	tostring(&os1, arg1);
	ymd_printf("${[yellow][   INFO   ] %s:%d Assert fail:}$ "
	           "(${[purple]%s}$) %s (${[purple]%s}$)\n"
			   "Expected : <${[purple]%s}$>\n"
			   "Actual   : <${[purple]%s}$>\n",
			   fn->u.core->file->land,
			   fn->u.core->line[up->pc - 1],
			   zos_buf(&os0),
			   op,
			   zos_buf(&os1),
			   zos_buf(&os0),
			   zos_buf(&os1));
	zos_final(&os0);
	zos_final(&os1);
	yut_raise(l);
}

static void yut_fail0(L) {
	int i;
	struct zostream os = ZOS_INIT;
	struct dyay *ax;
	if (l->top - l->stk < 3 || ymd_type(ymd_top(l, 0)) != T_DYAY) {
		return;
	}
	// print backtrace info
	ax = dyay_of(l, ymd_top(l, 0));
	for (i = 0; i < ax->count; ++i) {
		zos_append(&os, "\t", 1);
		tostring(&os, ax->elem + i);
		zos_append(&os, "\n", 1);
	}
	ymd_printf("${[yellow][   INFO   ] %s}$\n"
			   "Runtime error: %s\nBacktrace:\n%s",
			   kstr_of(l, ymd_top(l, 1))->land,
			   kstr_of(l, ymd_top(l, 2))->land,
			   zos_buf(&os));
	zos_final(&os);
	ymd_pop(l, 3);
}

static void yut_fail1(L, const char *expected,
                      const struct variable *arg0) {
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	struct zostream os = ZOS_INIT;
	ymd_printf("${[yellow][   INFO   ] %s:%d Assert fail, expected}$"
	           "${[purple]<%s>, unexpected}$${[purple]<%s>}$;\n",
			   fn->u.core->file->land,
	           fn->u.core->line[up->pc - 1],
	           expected,
	           tostring(&os, arg0));
	zos_final(&os);
	yut_raise(l);
}

static const char *format_interval(
		const struct timeval *start,
		const struct timeval *jiffx,
		char buf[],
		size_t len) {
	unsigned long long 
		jms = jiffx->tv_sec * 1000ULL + jiffx->tv_usec / 1000ULL,
	    bms = start->tv_sec * 1000ULL + start->tv_usec / 1000ULL;
	snprintf(buf, len, "%llu ms", jms - bms);
	return buf;
}

static int libx_Fail(L) {
	const struct kstr *arg0 = kstr_of(l, ymd_argv_get(l, 1));
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	ymd_printf("${[yellow][   INFO   ] %s:%d Fail: %s}$\n",
	           fn->u.core->file->land,
			   fn->u.core->line[up->pc - 1],
	           arg0->land);
	yut_raise(l);
	return 0;
}

static int libx_True(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (is_nil(arg0))
		yut_fail1(l, "not nil", arg0);
	if (ymd_type(arg0) == T_BOOL && !arg0->u.i)
		yut_fail1(l, "true or not nil", arg0);
	return 0;
}

static int libx_False(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (!is_nil(arg0) && (ymd_type(arg0) == T_BOOL && arg0->u.i))
		yut_fail1(l, "false or nil", arg0);
	return 0;
}

static int libx_Nil(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (!is_nil(arg0))
		yut_fail1(l, "nil", arg0);
	return 0;
}

static int libx_NotNil(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (is_nil(arg0))
		yut_fail1(l, "not nil", arg0);
	return 0;
}


#define EQ_LITERAL "=="
#define NE_LITERAL "!="
#define LT_LITERAL "<"
#define LE_LITERAL "<="
#define GT_LITERAL ">"
#define GE_LITERAL ">="
#define DEFINE_BIN_ASSERT(name, func, cond) \
static int libx_##name(L) { \
	struct variable *arg0 = ymd_argv_get(l, 1), \
					*arg1 = ymd_argv_get(l, 2); \
	if (func(arg0, arg1) cond) \
		yut_fail2(l, name##_LITERAL, arg0, arg1); \
	return 0; \
}

DEFINE_BIN_ASSERT(EQ, vm_equals,  == 0)
DEFINE_BIN_ASSERT(NE, vm_equals,  != 0)
DEFINE_BIN_ASSERT(LT, vm_compare, >= 0)
DEFINE_BIN_ASSERT(LE, vm_compare, >  0)
DEFINE_BIN_ASSERT(GT, vm_compare, <= 0)
DEFINE_BIN_ASSERT(GE, vm_compare, <  0)

#undef DEFINE_BIN_ASSERT

static struct func *yut_method(struct ymd_mach *vm, void *o,
                               const char *field) {
	struct variable *found = vm_mem(vm, o, field);
	if (is_nil(found) || ymd_type(found) != T_FUNC)
		return NULL;
	return func_x(found);
}

static int yut_call(struct ymd_context *l, struct variable *test,
		struct func *method) {
	int i;
	if (!method)
		return -1;
	setv_func(ymd_push(l), method);
	*ymd_push(l) = *test;
	i = ymd_xcall(l, 1);
	if (i < 0)
		return i;
	return ymd_adjust(l, 0, i);
}

static int yut_case(
		struct ymd_context *l,
		const char *clazz,
		const char *caze,
		struct variable *test,
		struct func *setup,
		struct func *teardown,
		struct func *unit) {
	char full_name[128], itv[32];
	struct timeval jiffx, start;

	strncpy(full_name, clazz, sizeof(full_name));
	strcat(full_name, ".");
	strcat(full_name, caze);
	yut_call(l, test, setup);
	ymd_printf("${[!green][ RUN      ]}$ %s\n", full_name);
	gettimeofday(&start, NULL);
	if (yut_call(l, test, unit) < 0) {
		yut_fail0(l);
		yut_fault();
		return -1;
	}
	gettimeofday(&jiffx, NULL);
	ymd_printf("${[!green][       OK ]}$ %s (%s)\n", full_name,
			format_interval(&start, &jiffx, itv, sizeof(itv)));
	yut_call(l, test, teardown);
	return 0;
}

struct yut_case_entry {
	struct func *fn;  // Test case function object.
	const char *name; // Test case name.
	int line;         // Defined function line.
};

static void yut_ordered_insert(const char *name, struct func *fn,
		struct yut_case_entry *ord, int *count) {
		int i, line = func_line(fn);
#define SET_ENT() \
	ord[i].name = name; \
	ord[i].fn   = fn; \
	ord[i].line = line
		// Insert ord into array `ord' in ordered.
		for (i = 0; i < *count; ++i) {
			if (line < ord[i].line) {
				memmove(ord + i + 1, ord + i, (*count - i) * sizeof(*ord));
				SET_ENT();
				++(*count);
				break;
			}
		}
		if (i == *count) {
			SET_ENT(); ++(*count);
		}
#undef SET_ENT

}

static struct yut_case_entry *yut_ordered_case(struct ymd_context *l,
		struct variable *test, int *k) {
	struct sknd *i;
	struct yut_case_entry *ord = calloc(skls_x(test)->count, sizeof(*ord));
	*k = 0;
	// Find all "test_" prefix function.
	for (i = skls_x(test)->head->fwd[0]; i != NULL; i = i->fwd[0]) {
		struct func *fn;
		const char *name = kstr_of(l, &i->k)->land;
		if (strstr(name, "test") != name || // Check prefix
				ymd_type(&i->v) != T_FUNC ||   // Check func type
				(fn = func_x(&i->v)) == NULL ||
				fn->is_c) // It's C function?
			continue;
		yut_ordered_insert(name, fn, ord, k);
	}
	return ord;
}

static int yut_test(struct ymd_mach *vm, const struct filter *filter,
		const char *clazz, struct variable *test) {
	int i, k, rv = 0;
	struct yut_case_entry *ord;
	struct func *setup, *teardown, *init, *final;
	struct ymd_context *l = ioslate(vm);
	if (ymd_type(test) != T_SKLS || !ftest(filter, clazz))
		return 0;
	// Get all of environmord functions.
	setup    = yut_method(vm, test->u.ref, "setup");
	teardown = yut_method(vm, test->u.ref, "teardown");
	init     = yut_method(vm, test->u.ref, "init");
	final    = yut_method(vm, test->u.ref, "final");
	if (setjmp(yut_jpt(l, vm_getg(vm, "Assert"))->jpt)) {
		yut_fault(); // Print failed message
		return -1; // Test Fail
	}
	// Sort all case by defined line number.
	if ((ord = yut_ordered_case(l, test, &k)) == NULL)
		return 0;
	yut_call(l, test, init); // ---- Initialize
	ymd_printf("${[!green][----------]}$ %s setup\n", clazz);
	for (i = 0; i < k; ++i) {
		if (!fcase(filter, clazz, ord[i].name))
			continue;
		if (yut_case(l, clazz, ord[i].name, test, setup, teardown,
					ord[i].fn) < 0) {
			--rv;
			break;
		}
	}
	yut_call(l, test, final); // ---- Finalize
	ymd_printf("${[!green][----------]}$ %s teardown\n\n", clazz);
	free(ord);
	return rv;
}

LIBC_BEGIN(YutAssertMethod)
	LIBC_ENTRY(Fail)
	LIBC_ENTRY(True)
	LIBC_ENTRY(False)
	LIBC_ENTRY(Nil)
	LIBC_ENTRY(NotNil)
	LIBC_ENTRY(EQ)
	LIBC_ENTRY(NE)
	LIBC_ENTRY(LT)
	LIBC_ENTRY(LE)
	LIBC_ENTRY(GT)
	LIBC_ENTRY(GE)
LIBC_END

int ymd_load_ut(struct ymd_mach *vm) {
	struct ymd_context *l = ioslate(vm);
	int rv;
	ymd_hmap(l, 0);
	ymd_kstr(l, "__cookie__", -1);
	ymd_mand(l, NULL, sizeof(struct yut_cookie), NULL);
	ymd_putf(l);
	ymd_kstr(l, "file", -1);
	ymd_int(l, -1);
	ymd_putf(l);
	ymd_kstr(l, "line", -1);
	ymd_int(l, -1);
	ymd_putf(l);
	rv = ymd_load_mem(l, "Assert", lbxYutAssertMethod);
	if (rv >= 0)
		ymd_putg(l, "Assert");
	else
		ymd_pop(l, 1);
	return rv;
}

int ymd_test(struct ymd_context *l, const char *pattern, int repeated,
		int argc, char *argv[]) {
	int t, rv = 0;
	struct filter filter;
	if (ymd_main(l, argc, argv) < 0)
		return -1;
	fparse(pattern, &filter);
	if (pattern && pattern[0])
		ymd_printf("${[!yellow]Use filter: %s}$\n", pattern);
	for (t = 0; t < repeated; ++t) {
		struct kvi *k = l->vm->global->item + (1 << l->vm->global->shift);
		struct kvi *i;
		if (repeated > 1)
			ymd_printf("${[!yellow]Repeated test %d of %d ...}$\n",
					t + 1, repeated);
		for (i = l->vm->global->item; i != k; ++i) {
			const char *clazz;
			if (!i->flag)
				continue;
			clazz = kstr_of(l, &i->k)->land;
			if (strstr(clazz, "Test")) { // Check "Test" prefix
				if (yut_test(l->vm, &filter, clazz, &i->v) < 0) {
					rv = 1;
					goto final;
				}
				break;
			}
		}
	}
final:
	free((void*)filter.test_pattern);
	free((void*)filter.case_pattern);
	return rv;
}

