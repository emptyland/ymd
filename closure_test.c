#include "yut.h"
#include "yut_rand.h"
#include "state.h"
#include "memory.h"
#include "value.h"
#include "assembly.h"
#include <stdlib.h>

static struct func *fn;
static struct chunk *core;

static int test_func_setup() {
	fn = func_new(NULL);
	core = fn->u.core;
	return 0;
}

static int test_func_local_constant_1() {
	ASSERT_EQ(int, func_kz(fn, "a", -1), 0);
	ASSERT_EQ(int, func_kz(fn, "b", -1), 1);
	ASSERT_EQ(int, func_kz(fn, "c", -1), 2);
	ASSERT_EQ(int, func_kz(fn, "b", -1), 1);

	ASSERT_EQ(int, func_add_lz(fn, "a"), 0);
	ASSERT_EQ(int, func_add_lz(fn, "b"), 1);
	ASSERT_EQ(int, func_add_lz(fn, "c"), 2);
	ASSERT_EQ(int, func_add_lz(fn, "b"), -1);

	func_shrink(fn);

	ASSERT_EQ(int, func_kz(fn, "a", -1), 0);
	ASSERT_EQ(int, func_kz(fn, "b", -1), 1);
	ASSERT_EQ(int, func_kz(fn, "c", -1), 2);
	ASSERT_TRUE(core->kz[0] == ymd_kstr("a", -1));
	ASSERT_TRUE(core->kz[1] == ymd_kstr("b", -1));
	ASSERT_TRUE(core->kz[2] == ymd_kstr("c", -1));

	ASSERT_EQ(int, func_find_lz(fn, "a"), 0);
	ASSERT_EQ(int, func_find_lz(fn, "b"), 1);
	ASSERT_EQ(int, func_find_lz(fn, "c"), 2);
	ASSERT_TRUE(core->lz[0] == ymd_kstr("a", -1));
	ASSERT_TRUE(core->lz[1] == ymd_kstr("b", -1));
	ASSERT_TRUE(core->lz[2] == ymd_kstr("c", -1));
	return 0;
}

static int test_func_local_constant_2() {
#define BATCH_COUNT 260
	int i = BATCH_COUNT;
	while (i--) {
		char key[32];
		snprintf(key, sizeof(key), "key.%d", i);
		ASSERT_EQ(int, func_kz(fn, key, -1), BATCH_COUNT - 1 - i);
		ASSERT_EQ(int, func_add_lz(fn, key), BATCH_COUNT - 1 - i);
	}
	func_shrink(fn);
	i = BATCH_COUNT;
	while (i--) {
		char key[32];
		int index = BATCH_COUNT - 1 - i;
		snprintf(key, sizeof(key), "key.%d", i);
		ASSERT_EQ(int, func_kz(fn, key, -1), index);
		ASSERT_TRUE(core->kz[index] == ymd_kstr(key, -1));
		ASSERT_EQ(int, func_find_lz(fn, key), index);
		ASSERT_TRUE(core->lz[index] == ymd_kstr(key, -1));
	}
#undef BATCH_COUNT
	return 0;
}

#define gc(x) ((struct gc_node *)x)

/*
static int test_func_bind_1() {
	struct variable var;

	var.type = T_INT;
	var.value.i = 3141516927;
	ASSERT_EQ(int, func_bind(fn, &var), 1);

	var.type = T_EXT;
	var.value.ext = fn;
	ASSERT_EQ(int, func_bind(fn, &var), 2);

	var.type = T_KSTR;
	var.value.ref = gc(kstr_new("Constant String", -1));
	ASSERT_EQ(int, func_bind(fn, &var), 3);

	var.type = T_SKLS;
	var.value.ref = gc(skls_new());
	ASSERT_EQ(int, func_bind(fn, &var), 4);

	ASSERT_EQ(uint, fn->n_bind, 4);
	ASSERT_EQ(uint, fn->bind[0].type, T_INT);
	ASSERT_EQ(large, fn->bind[0].value.i, 3141516927LL);
	ASSERT_EQ(uint, fn->bind[1].type, T_EXT);
	ASSERT_TRUE(fn->bind[1].value.ext == fn);
	ASSERT_EQ(uint, fn->bind[2].type, T_KSTR);
	ASSERT_STREQ(kstr_of(fn->bind + 2)->land, "Constant String");
	ASSERT_EQ(uint, fn->bind[3].type, T_SKLS);
	return 0;
}
*/

/*
static int test_func_bind_2() {
#define BATCH_COUNT 300
	int i = BATCH_COUNT;
	while (i--) {
		struct variable var;
		var.type = T_INT;
		var.value.i = i;
		ASSERT_EQ(int, func_bind(fn, &var), BATCH_COUNT - i);
	}
	for (i = 0; i < BATCH_COUNT; ++i) {
		ASSERT_EQ(uint, fn->bind[i].type, T_INT);
		ASSERT_EQ(large, fn->bind[i].value.i, BATCH_COUNT - 1 - i);
	}
#undef BATCH_COUNT
	return 0;
}
*/

static int test_func_emit_1() {
	func_emit(fn, emitAfP(PUSH, INT, 0));
	func_emit(fn, emitAfP(PUSH, INT, 1));
	func_emit(fn, emitAfP(PUSH, INT, 2));
	func_emit(fn, emitAfP(PUSH, ZSTR, 0));
	func_emit(fn, emitAfP(PUSH, LOCAL, 0));
	func_emit(fn, emitAf(PUSH, NIL));
	ASSERT_EQ(int, core->kinst, 6);
	ASSERT_EQ(uint, core->inst[0], emitAfP(PUSH, INT, 0));
	ASSERT_EQ(uint, core->inst[5], emitAf(PUSH, NIL));
	return 0;
}

static int test_func_emit_2() {
#define BATCH_COUNT 2000
	int i = BATCH_COUNT;
	while (i--)
		func_emit(fn, emitAf(PUSH, NIL));
	ASSERT_EQ(int, core->kinst, BATCH_COUNT);
	func_shrink(fn);
	i = BATCH_COUNT;
	while (i--)
		ASSERT_EQ(uint, core->inst[i], emitAf(PUSH, NIL));
#undef BATCH_COUNT
	return 0;
}

TEST_BEGIN_WITH(test_func_setup, NULL)
	TEST_ENTRY(func_local_constant_1, normal)
	TEST_ENTRY(func_local_constant_2, batch)
	//TEST_ENTRY(func_bind_1, normal)
	//TEST_ENTRY(func_bind_2, batch)
	TEST_ENTRY(func_emit_1, normal)
	TEST_ENTRY(func_emit_2, batch)
TEST_END
