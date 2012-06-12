#include "disassembly.h"
#include "value.h"
#include "memory.h"
#include "state.h"
#include "libc.h"
#include "yut_rand.h"
#include "yut.h"

static int test_call_run_1() {
	//int i;
	struct func *fn = func_compile(stdin);
	ASSERT_NOTNULL(fn);
	ymd_load_lib(lbxBuiltin);
	//func_proto(fn);
	/*dis_func(stdout, fn);
	for (i = 0; i < vm()->n_fn; ++i) {
		printf("====[%s]====\n", vm()->fn[i]->proto->land);
		dis_func(stdout, vm()->fn[i]);
	}*/
	func_call(fn, 0);
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(call_run_1, normal)
TEST_END
