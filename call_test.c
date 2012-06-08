#include "disassembly.h"
#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

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
	TEST_ENTRY(call_run, normal)
TEST_END
