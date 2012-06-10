#include "disassembly.h"
#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

static int nafn_puts(struct context *l) {
	struct func *fn = l->info->run;
	int i = func_find_lz(fn, "argv");
	struct variable *var = l->info->loc + i;
	struct dyay *argv = dyay_of(var);

	for (i = 0; i < argv->count; ++i)
		printf("%lld ", argv->elem[i].value.i);
	printf("\n");
	return 0;
}

static int nafn_doom(struct context *l) {
	(void)l;
	printf("%s:%d ok\n", __FILE__, __LINE__);
	return 0;
}

static int test_call_nafn_run() {
	int nrv;
	struct func *pgm, *fn = func_new(nafn_doom);
	struct variable var, *rv;

	//nrv = func_call(fn, 0);
	//ASSERT_EQ(int, 0, nrv);

	nrv = func_add_lz(fn, "argv");
	ASSERT_GE(int, nrv, 0);
	var.type = T_KSTR;
	var.value.ref = (struct gc_node *)ymd_kstr("doom", -1);
	rv = hmap_get(vm()->global, &var);
	rv->type = T_FUNC;
	rv->value.ref = (struct gc_node *)fn;

	fn = func_new(nafn_puts);
	nrv = func_add_lz(fn, "argv");
	ASSERT_GE(int, nrv, 0);
	var.type = T_KSTR;
	var.value.ref = (struct gc_node *)ymd_kstr("puts", -1);
	rv = hmap_get(vm()->global, &var);
	rv->type = T_FUNC;
	rv->value.ref = (struct gc_node *)fn;

	pgm = func_compile(stdin);
	ASSERT_NOTNULL(pgm);
	nrv = func_add_lz(pgm, "argv");
	ASSERT_GE(int, nrv, 0);
	func_call(pgm, 0);
	return 0;
}

static int test_call_run() {
	int i;
	struct func *fn = func_compile(stdin);
	ASSERT_NOTNULL(fn);
	//func_dump(fn, stdout);
	dis_func(stdout, fn);
	for (i = 0; i < vm()->n_fn; ++i) {
		printf("====[%s]====\n", vm()->fn[i]->proto->land);
		dis_func(stdout, vm()->fn[i]);
	}
	return 0;
}

TEST_BEGIN
	//TEST_ENTRY(call_run, normal)
	TEST_ENTRY(call_nafn_run, normal)
TEST_END
