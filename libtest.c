#include "tostring.h"
#include "state.h"
#include "memory.h"
#include "value.h"
#include "libc.h"
#include "print.h"
#include <setjmp.h>

#define L struct context *l

struct yut_cookie {
	jmp_buf jpt;
};

static struct hmap *yut_assert_new() {
	struct hmap *o = hmap_new(0);
	vset_hmap(ymd_putg("Assert"), o);
	return o;
}

static struct mand *yut_cookie_new() {
	struct mand *cookie = mand_new(NULL, sizeof(struct yut_cookie), NULL);
	return cookie;
}

static struct yut_cookie *yut_jpt(struct variable *self) {
	struct hmap *o = hmap_of(self);
	struct mand *cookie = mand_of(ymd_mem(o, "__cookie__"));
	return (struct yut_cookie *)cookie->land;
}

static void yut_raise(L) {
	struct yut_cookie *cookie = yut_jpt(ymd_argv_get(l, 0));
	longjmp(cookie->jpt, 1);
}

static void yut_fail2(L, const struct variable *arg0,
                      const struct variable *arg1) {
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	struct fmtx fx0 = FMTX_INIT, fx1 = FMTX_INIT;
	ymd_printf(yYELLOW"[ INFO ] :%d Assert fail, expected"yEND
			   yPURPLE"<%s>"yEND
			   yYELLOW", unexpected"yEND
			   yPURPLE"<%s>"yEND
			   ";\n",
			   fn->u.core->line[up->pc - 1],
			   tostring(&fx0, arg0),
			   tostring(&fx1, arg1));
	fmtx_final(&fx0);
	fmtx_final(&fx1);
	yut_raise(l);
}

static void yut_fail1(L, const char *expected,
                      const struct variable *arg0) {
	struct call_info *up = l->info->chain;
	struct func *fn = up->run;
	struct fmtx fx = FMTX_INIT;
	ymd_printf(yYELLOW"[ INFO ] :%d Assert fail, expected"yEND
			   yPURPLE"<%s>"yEND
			   yYELLOW", unexpected"yEND
			   yPURPLE"<%s>"yEND
			   ";\n",
			   fn->u.core->line[up->pc - 1],
			   expected,
			   tostring(&fx, arg0));
	fmtx_final(&fx);
	yut_raise(l);
}

static int libx_True(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (is_nil(arg0))
		yut_fail1(l, "not nil", arg0);
	if (arg0->type == T_BOOL && !arg0->value.i)
		yut_fail1(l, "true or not nil", arg0);
	return 0;
}

static int libx_False(L) {
	struct variable *arg0 = ymd_argv_get(l, 1);
	if (!is_nil(arg0) && (arg0->type == T_BOOL && arg0->value.i))
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

static int libx_EQ(L) {
	struct variable *arg0 = ymd_argv_get(l, 1),
					*arg1 = ymd_argv_get(l, 2);
	if (!equals(arg0, arg1))
		yut_fail2(l, arg0, arg1);
	return 0;
}

static struct func *yut_method(void *o, const char *field) {
	struct variable *found = ymd_mem(o, field);
	if (is_nil(found) || found->type != T_FUNC)
		return NULL;
	return func_x(found);
}

static int yut_call(struct variable *test, struct func *method) {
	struct context *l = ioslate();
	if (!method)
		return -1;
	vset_func(ymd_push(l), method);
	*ymd_push(l) = *test;
	return func_call(method, 1, 0);
}

static int yut_case(
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
	ymd_printf(yGREEN"[======]"yEND" Test "yPURPLE"%s"yEND" setup.\n",
			   full_name);
	yut_call(test, setup);
	ymd_printf(yGREEN"[ RUN  ]"yEND" Running ...\n");
	yut_call(test, unit);
	ymd_printf(yGREEN"[   OK ]"yEND" Passed!\n");
	yut_call(test, teardown);
	ymd_printf(yGREEN"[------]"yEND" Test teardown.\n");
	return 0;
}

static int yut_test(const char *clazz, struct variable *test) {
	struct sknd *i;
	struct func *setup, *teardown;
	if (test->type != T_SKLS)
		return 0;
	setup = yut_method(test->value.ref, "setup");
	teardown = yut_method(test->value.ref, "teardown");
	if (setjmp(yut_jpt(ymd_getg("Assert"))->jpt))
		return -1; // Test Fail
	for (i = skls_x(test)->head->fwd[0]; i != NULL; i = i->fwd[0]) {
		const char *caze = kstr_of(&i->k)->land;
		if (!strstr(caze, "test") || i->v.type != T_FUNC)
			continue;
		yut_case(clazz, caze, test, setup, teardown, func_x(&i->v));
	}
	return 0;
}

LIBC_BEGIN(YutAssertMethod)
	LIBC_ENTRY(True)
	LIBC_ENTRY(False)
	LIBC_ENTRY(Nil)
	LIBC_ENTRY(NotNil)
	LIBC_ENTRY(EQ)
LIBC_END

int ymd_load_ut() {
	struct hmap *o = yut_assert_new();
	vset_mand(ymd_def(o, "__cookie__"), yut_cookie_new());
	vset_int(ymd_def(o, "file"), -1);
	vset_int(ymd_def(o, "line"), -1);
	return ymd_load_mem("Assert", o, lbxYutAssertMethod);
}

int ymd_test(struct func *fn, int argc, char *argv[]) {
	struct kvi *i, *kend = vm()->global->item + (1 << vm()->global->shift);
	(void)argc;
	(void)argv;
	func_main(fn, argc, argv);
	for (i = vm()->global->item; i != kend; ++i) {
		const char *clazz;
		if (!i->flag)
			continue;
		clazz = kstr_of(&i->k)->land;
		if (strstr(clazz, "Test")) {
			if (yut_test(clazz, &i->v) < 0)
				return -1;
		}
	}
	return 0;
}

