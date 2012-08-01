#include "pickle.h"
#include "core.h"
#include "zstream.h"
#include "assembly.h"
#include "yut.h"

#define CHECK_OK &ok); ASSERT_TRUE(ok
static int test_dump_simple () {
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(NULL, NULL, 0);
	struct variable k;
	int i, ok = 1;
	vset_nil(&k);
	i = ymd_serialize(&os, &k, CHECK_OK);
	is.buf = os.kbuf;
	is.max = os.last;
	ASSERT_EQ(int, i, 1);
	ASSERT_EQ(uint, zis_u32(&is), T_NIL);

	vset_int(&k, 0x40LL);
	zos_final(&os);
	i = ymd_serialize(&os, &k, CHECK_OK);
	zis_pipe(&is, &os);
	ASSERT_EQ(int, i, 3);
	ASSERT_EQ(uint, zis_u32(&is), T_INT);
	ASSERT_EQ(large, zis_i64(&is), 0x40LL);

	vset_bool(&k, 1);
	zos_final(&os);
	i = ymd_serialize(&os, &k, CHECK_OK);
	zis_pipe(&is, &os);
	ASSERT_EQ(int, i, 2);
	ASSERT_EQ(uint, zis_u32(&is), T_BOOL);
	ASSERT_EQ(ularge, zis_u64(&is), 1);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_dump_kstr () {
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(NULL, NULL, 0);
	const char *z = "Sucking Star Big Law";
	char buf[1024] = {0};
	struct kstr *kz = kstr_fetch(tvm, z, -1);
	int i = ymd_dump_kstr(&os, kz);

	ASSERT_EQ(int, i, 1 + 1 + kz->len);
	zis_pipe(&is, &os);
	ASSERT_EQ(uint, zis_u32(&is), T_KSTR);
	ASSERT_EQ(int, zis_u32(&is), kz->len);
	zis_fetch(&is, buf, kz->len);
	ASSERT_STREQ(buf, z);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_dump_func () {
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(NULL, NULL, 0);
	char z[1024] = {0};
	int i, ok = 1;
	struct chunk *x = mm_zalloc(tvm, 1, sizeof(*x));
	struct func *o = func_new(tvm, x, "foo");
	o->n_bind = 2;
	vset_int(func_bval(tvm, o, 0), 1LL);
	vset_bool(func_bval(tvm, o, 1), 0LL);
	blk_emit(tvm, x, emitAfP(PUSH, KVAL, 0), 0);
	blk_emit(tvm, x, emitAP(RET, 1), 1);
	blk_add_lz(tvm, x, "i");
	blk_add_lz(tvm, x, "k");
	blk_kz(tvm, x, "bar", 3);
	blk_kz(tvm, x, "baz", 3);
	blk_ki(tvm, x, 10028);
	x->kargs = 2;
	x->file  = kstr_fetch(tvm, "foo.ymd", -1);
	blk_shrink(tvm, x);

	i = ymd_dump_func(&os, o, CHECK_OK);
	zis_pipe(&is, &os);

	// :tt
	ASSERT_EQ(uint,  zis_u32(&is), T_FUNC);
	// :proto
	ASSERT_EQ(uint,  zis_u32(&is), T_KSTR);
	ASSERT_EQ(int,   zis_u32(&is), o->proto->len);
	zis_fetch(&is, z, o->proto->len);
	ASSERT_STREQ(z, o->proto->land);
	// :bind
	ASSERT_EQ(int,    zis_u32(&is), 2);
	ASSERT_EQ(uint,   zis_u32(&is), T_INT);
	ASSERT_EQ(large,  zis_i64(&is), 1LL);
	ASSERT_EQ(uint,   zis_u32(&is), T_BOOL);
	ASSERT_EQ(ularge, zis_u64(&is), 0LL);
	// :chunk
	// :inst
	ASSERT_EQ(int,    zis_u32(&is), 2);
	zis_fetch(&is, z, 2 * sizeof(ymd_inst_t));
	ASSERT_EQ(uint,   ((ymd_inst_t*)z)[0], emitAfP(PUSH, KVAL, 0));
	ASSERT_EQ(uint,   ((ymd_inst_t*)z)[1], emitAP(RET, 1));
	ASSERT_EQ(int,    zis_u32(&is), 0);
	ASSERT_EQ(int,    zis_u32(&is), 1);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_dump_dyay () {
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(NULL, NULL, 0);
	const char *z = "Hello, World!\n";
	char buf[1024] = {0};
	struct dyay *ax = dyay_new(tvm, 4);
	int ok = 1;
	vset_nil(dyay_add(tvm, ax));
	vset_int(dyay_add(tvm, ax), 0x40);
	vset_bool(dyay_add(tvm, ax), 0);
	vset_kstr(dyay_add(tvm, ax), kstr_fetch(tvm, z, -1));

	ymd_dump_dyay(&os, ax, CHECK_OK);
	zis_pipe(&is, &os);
	ASSERT_EQ(uint,   zis_u32(&is), T_DYAY);
	ASSERT_EQ(int,    zis_u32(&is), ax->count);
	ASSERT_EQ(uint,   zis_u32(&is), T_NIL);
	ASSERT_EQ(uint,   zis_u32(&is), T_INT);
	ASSERT_EQ(large,  zis_i64(&is), 0x40);
	ASSERT_EQ(uint,   zis_u32(&is), T_BOOL);
	ASSERT_EQ(ularge, zis_u64(&is), 0);
	ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
	ASSERT_EQ(ulong,  zis_u32(&is), strlen(z));
	zis_fetch(&is, buf, strlen(z));
	ASSERT_STREQ(buf, z);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_dump_o (void *o, unsigned tt) {
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(NULL, NULL, 0);
	int i, ok = 1;
	char z[1024] = {0};
	vset_int(vm_def(tvm, o, "0-1st"), 1);
	vset_bool(vm_def(tvm, o, "1-switch"), 1);
	vset_kstr(vm_def(tvm, o, "2-name"), kstr_fetch(tvm, "John", -1));
	vset_int(vm_def(tvm, o, "3-id"), 90048);
	vset_bool(vm_def(tvm, o, "4-maybe"), 0);
	if (tt == T_HMAP) {
		i = ymd_dump_hmap(&os, o, CHECK_OK);
	} else {
		i = ymd_dump_skls(&os, o, CHECK_OK);
		zis_pipe(&is, &os);
		ASSERT_EQ(uint,   zis_u32(&is), tt);
		ASSERT_EQ(uint,   zis_u32(&is), 5);
		//==+==
		ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
		ASSERT_EQ(int,    zis_u32(&is), 5);
		zis_fetch(&is, z, 5); z[5] = 0;
		ASSERT_STREQ(z, "0-1st");
		//--+--
		ASSERT_EQ(uint,   zis_u32(&is), T_INT);
		ASSERT_EQ(large,  zis_i64(&is), 1);
		//==+==
		ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
		ASSERT_EQ(int,    zis_u32(&is), 8);
		zis_fetch(&is, z, 8); z[8] = 0;
		ASSERT_STREQ(z, "1-switch");
		//--+--
		ASSERT_EQ(uint,   zis_u32(&is), T_BOOL);
		ASSERT_EQ(ularge, zis_u64(&is), 1);
		//==+==
		ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
		ASSERT_EQ(int,    zis_u32(&is), 6);
		zis_fetch(&is, z, 6); z[6] = 0;
		ASSERT_STREQ(z, "2-name");
		//--+--
		ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
		ASSERT_EQ(int,    zis_u32(&is), 4);
		zis_fetch(&is, z, 4); z[4] = 0;
		ASSERT_STREQ(z, "John");
		//==+==
		ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
		ASSERT_EQ(int,    zis_u32(&is), 4);
		zis_fetch(&is, z, 4); z[4] = 0;
		ASSERT_STREQ(z, "3-id");
		//--+--
		ASSERT_EQ(uint,   zis_u32(&is), T_INT);
		ASSERT_EQ(large,  zis_i64(&is), 90048);
		//==+==
		ASSERT_EQ(uint,   zis_u32(&is), T_KSTR);
		ASSERT_EQ(int,    zis_u32(&is), 7);
		zis_fetch(&is, z, 7); z[7] = 0;
		ASSERT_STREQ(z, "4-maybe");
		//--+--
		ASSERT_EQ(uint,   zis_u32(&is), T_BOOL);
		ASSERT_EQ(ularge, zis_u64(&is), 0);
		//=====
		ASSERT_EQ(int, zis_remain(&is), 0);
	}
	ASSERT_EQ(int, i, 58);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_dump_hmap () {
	return test_dump_o (hmap_new(tvm, 0), T_HMAP);
}

static int test_dump_skls () {
	return test_dump_o (skls_new(tvm), T_SKLS);
}

static int test_load_simple () {
	struct ymd_context *l = ioslate(tvm);
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(l, NULL, 0);
	int i, ok = 1;

	zos_u32(&os, T_NIL); // nil
	zos_u32(&os, T_INT); // -1
	zos_i64(&os, 0x8040);
	zos_u32(&os, T_BOOL); // true
	zos_u64(&os, 1);
	zos_u32(&os, T_BOOL); // false
	zos_u64(&os, 0);

	zis_pipe(&is, &os);

	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint, ymd_top(l, 0)->type, T_NIL);
	ymd_pop(l, 1);

	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint,  ymd_top(l, 0)->type,    T_INT);
	ASSERT_EQ(large, ymd_top(l, 0)->u.i, 0x8040);

	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint,  ymd_top(l, 0)->type,    T_BOOL);
	ASSERT_EQ(large, ymd_top(l, 0)->u.i, 1);

	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint,  ymd_top(l, 0)->type,    T_BOOL);
	ASSERT_EQ(large, ymd_top(l, 0)->u.i, 0);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_load_kstr () {
	struct ymd_context *l = ioslate(tvm);
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(l, NULL, 0);
	int i, ok = 1;

	zos_u32(&os, T_KSTR); // "01234567"
	zos_u32(&os, 8);
	zos_append(&os, "01234567", 8);

	zis_pipe(&is, &os);

	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint, ymd_top(l, 0)->type, T_KSTR);
	ASSERT_STREQ(kstr_k(ymd_top(l, 0))->land, "01234567");
	ymd_pop(l, 1);

	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_load_dyay () {
	struct ymd_context *l = ioslate(tvm);
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(l, NULL, 0);
	int i, ok = 1;
	struct dyay *ax;

	zos_u32(&os, T_DYAY);
	zos_u32(&os, 3);

	zos_u32(&os, T_INT);
	zos_i64(&os, 0x20108040L);

	zos_u32(&os, T_BOOL);
	zos_u64(&os, 1);

	zos_u32(&os, T_KSTR);
	zos_u32(&os, 8);
	zos_append(&os, "01234567", 8);

	zis_pipe(&is, &os);

	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint, ymd_top(l, 0)->type, T_DYAY);
	ax = dyay_x(ymd_top(l, 0));
	ASSERT_EQ(int,   ax->count, 3);

	ASSERT_EQ(uint,  ax->elem[0].type, T_INT);
	ASSERT_EQ(large, ax->elem[0].u.i, 0x20108040LL);

	ASSERT_EQ(uint,  ax->elem[1].type, T_BOOL);
	ASSERT_EQ(large, ax->elem[1].u.i, 1LL);

	ASSERT_EQ(uint,  ax->elem[2].type, T_KSTR);
	ASSERT_STREQ(kstr_k(ax->elem + 2)->land, "01234567");
	ymd_pop(l, 1);

	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_load_o (int tt) {
	struct ymd_context *l = ioslate(tvm);
	struct zostream os = ZOS_INIT;
	struct zistream is = ZIS_INIT(l, NULL, 0);
	int i, ok = 1;
	void *o;

	zos_u32(&os, tt);
	zos_u32(&os, 3);

	zos_u32(&os, T_KSTR);
	zos_u32(&os, 3);
	zos_append(&os, "1st", 3);
	zos_u32(&os, T_INT);
	zos_i64(&os, 1LL);

	zos_u32(&os, T_KSTR);
	zos_u32(&os, 3);
	zos_append(&os, "2nd", 3);
	zos_u32(&os, T_INT);
	zos_i64(&os, 2LL);

	zos_u32(&os, T_KSTR);
	zos_u32(&os, 3);
	zos_append(&os, "3rd", 3);
	zos_u32(&os, T_INT);
	zos_i64(&os, 3LL);

	zis_pipe(&is, &os);
	i = ymd_parse(&is, CHECK_OK);
	ASSERT_EQ(uint, ymd_top(l, 0)->type, tt);
	o = ymd_top(l, 0)->u.ref;

	ASSERT_EQ(uint,  vm_mem(l->vm, o, "1st")->type, T_INT);
	ASSERT_EQ(large, vm_mem(l->vm, o, "1st")->u.i, 1LL);

	ASSERT_EQ(uint,  vm_mem(l->vm, o, "2nd")->type, T_INT);
	ASSERT_EQ(large, vm_mem(l->vm, o, "2nd")->u.i, 2LL);

	ASSERT_EQ(uint,  vm_mem(l->vm, o, "3rd")->type, T_INT);
	ASSERT_EQ(large, vm_mem(l->vm, o, "3rd")->u.i, 3LL);

	ymd_pop(l, 1);
	zis_final(&is);
	zos_final(&os);
	return 0;
}

static int test_load_hmap () {
	return test_load_o (T_HMAP);
}

static int test_load_skls () {
	return test_load_o (T_SKLS);
}

static int pickle_setup () {
	gc_active(tvm, GC_PAUSE);
	return 0;
}

static void pickle_teardown () {
	gc_active(tvm, GC_IDLE);
}

TEST_BEGIN_WITH(pickle_setup, pickle_teardown)
	TEST_ENTRY(dump_simple, normal)
	TEST_ENTRY(dump_kstr, normal)
	TEST_ENTRY(dump_func, normal)
	TEST_ENTRY(dump_dyay, normal)
	TEST_ENTRY(dump_hmap, normal)
	TEST_ENTRY(dump_skls, normal)
	TEST_ENTRY(load_simple, normal)
	TEST_ENTRY(load_kstr, normal)
	TEST_ENTRY(load_dyay, normal)
	TEST_ENTRY(load_hmap, normal)
	TEST_ENTRY(load_skls, normal)
TEST_END

