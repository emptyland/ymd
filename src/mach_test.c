#include "core.h"
#include "yut_rand.h"
#include "mach_test.def"

static struct ymd_mach *setup() {
	struct ymd_mach *vm = ymd_init();
	gc_active(vm, +1);
	return vm;
}

static void teardown(struct ymd_mach *vm) {
	gc_active(vm, -1);
	ymd_final(vm);
}

static int test_dynamic_stack (struct ymd_mach *vm) {
	struct ymd_context *l = ioslate(vm);
	size_t i = YMD_INIT_STACK;

	ASSERT_EQ(ulong, i, l->kstk);

	while (i--) {
		struct variable *top = ymd_push(l);
		ASSERT_EQ(int, T_NIL, top->tt);
	}
	ymd_push(l);

	ASSERT_EQ(ulong, YMD_INIT_STACK * 2, l->kstk);
	return 0;
}

static int test_stack_incrment (struct ymd_mach *vm) {
	struct ymd_context *l = ioslate(vm);
	size_t i = YMD_INIT_STACK - 1;
	const ymd_int_t pi = 31415926;
	const ymd_int_t k  = 214;

	while (i--)
		ymd_push(l);
	ymd_int(l, pi);
	ymd_int(l, k);

	ASSERT_EQ(ulong, YMD_INIT_STACK * 2, l->kstk);
	ASSERT_EQ(large, k, ymd_top(l, 0)->u.i);

	ymd_pop(l, 1);
	ASSERT_EQ(large, pi, ymd_top(l, 0)->u.i);

	ymd_pop(l, 1);
	ASSERT_EQ(ulong, YMD_INIT_STACK * 2, l->kstk);

	i = YMD_INIT_STACK - (YMD_INIT_STACK / 4);
	ymd_pop(l, i);
	ASSERT_EQ(ulong, YMD_INIT_STACK, l->kstk);
	return 0;
}
