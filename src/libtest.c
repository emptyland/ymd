#include "tostring.h"
#include "core.h"
#include "libc.h"
#include "print.h"
#include <setjmp.h>

#define L struct ymd_context *l

struct yut_cookie {
	jmp_buf jpt;
};

static inline void yut_fault() {
	ymd_printf("${[red][ FAIL ]}$ Test fail, stop all.\n");
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

// [  XXX ] Line:%d <%s> Assert fail.
static void yut_fail2(L, const char *op,
                      const struct variable *arg0,
                      const struct variable *arg1) {
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	struct zostream os0 = ZOS_INIT, os1 = ZOS_INIT;
	tostring(&os0, arg0);
	tostring(&os1, arg1);
	ymd_printf("${[yellow][  XXX ] %s:%d Assert fail: }$"
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
	if (l->top - l->stk < 3 || TYPEV(ymd_top(l, 0)) != T_DYAY) {
		return;
	}
	// print backtrace info
	ax = dyay_of(l, ymd_top(l, 0));
	for (i = 0; i < ax->count; ++i) {
		zos_append(&os, "\t", 1);
		tostring(&os, ax->elem + i);
		zos_append(&os, "\n", 1);
	}
	ymd_printf("${[yellow][  XXX ] %s}$\n"
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
	ymd_printf("${[yellow][  XXX ] %s:%d Assert fail, expected}$"
	           "${[purple]<%s>, unexpected}$${[purple]<%s>}$;\n",
			   fn->u.core->file->land,
	           fn->u.core->line[up->pc - 1],
	           expected,
	           tostring(&os, arg0));
	zos_final(&os);
	yut_raise(l);
}

static int libx_Fail(L) {
	const struct kstr *arg0 = kstr_of(l, ymd_argv_get(l, 1));
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	ymd_printf("${[yellow][  XXX ] %s:%d Fail: %s}$\n",
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
	if (TYPEV(arg0) == T_BOOL && !arg0->u.i)
		yut_fail1(l, "true or not nil", arg0);
	return 0;
}

static int libx_False(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (!is_nil(arg0) && (TYPEV(arg0) == T_BOOL && arg0->u.i))
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

DEFINE_BIN_ASSERT(EQ, equals,  == 0)
DEFINE_BIN_ASSERT(NE, equals,  != 0)
DEFINE_BIN_ASSERT(LT, compare, >= 0)
DEFINE_BIN_ASSERT(LE, compare, > 0 )
DEFINE_BIN_ASSERT(GT, compare, <= 0)
DEFINE_BIN_ASSERT(GE, compare, < 0 )

#undef DEFINE_BIN_ASSERT

static struct func *yut_method(struct ymd_mach *vm, void *o,
                               const char *field) {
	struct variable *found = vm_mem(vm, o, field);
	if (is_nil(found) || TYPEV(found) != T_FUNC)
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
	char full_name[128];
	strncpy(full_name, clazz, sizeof(full_name));
	strcat(full_name, ".");
	strcat(full_name, caze);
	ymd_printf("${[green][======]}$ Test ${[purple]%s}$ setup.\n",
			   full_name);
	yut_call(l, test, setup);
	ymd_printf("${[green][ RUN  ]}$ Running ...\n");
	if (yut_call(l, test, unit) < 0) {
		yut_fail0(l);
		yut_fault();
		return -1;
	}
	ymd_printf("${[green][   OK ]}$ Passed!\n");
	yut_call(l, test, teardown);
	ymd_printf("${[green][------]}$ Test teardown.\n");
	return 0;
}

static int yut_test(struct ymd_mach *vm, const char *clazz,
                    struct variable *test) {
	struct sknd *i;
	struct func *setup, *teardown, *init, *final;
	struct ymd_context *l = ioslate(vm);
	if (TYPEV(test) != T_SKLS)
		return 0;
	setup    = yut_method(vm, test->u.ref, "setup");
	teardown = yut_method(vm, test->u.ref, "teardown");
	init     = yut_method(vm, test->u.ref, "init");
	final    = yut_method(vm, test->u.ref, "final");
	if (setjmp(yut_jpt(l, vm_getg(vm, "Assert"))->jpt)) {
		yut_fault(); // Print failed message
		return -1; // Test Fail
	}
	yut_call(l, test, init); // ---- Initialize
	for (i = skls_x(test)->head->fwd[0]; i != NULL; i = i->fwd[0]) {
		const char *caze = kstr_of(l, &i->k)->land;
		if (!strstr(caze, "test") || TYPEV(&i->v) != T_FUNC)
			continue;
		if (yut_case(l, clazz, caze, test, setup, teardown,
			         func_x(&i->v)) < 0)
			break;
	}
	yut_call(l, test, final); // ---- Finalize
	return 0;
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

int ymd_test(struct ymd_context *l, int argc, char *argv[]) {
	struct kvi *i, *k;
	if (ymd_main(l, argc, argv) < 0)
		return -1;
	k = l->vm->global->item + (1 << l->vm->global->shift);
	for (i = l->vm->global->item; i != k; ++i) {
		const char *clazz;
		if (!i->flag)
			continue;
		clazz = kstr_of(l, &i->k)->land;
		if (strstr(clazz, "Test")) {
			if (yut_test(l->vm, clazz, &i->v) < 0)
				return -1;
			break;
		}
	}
	return 0;
}

