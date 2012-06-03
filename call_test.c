#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

static int test_call_run() {
	struct func *fn = func_compile(stdin);
	ASSERT_NOTNULL(fn);
	
	func_dump(fn, stdout);
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(call_run, normal)
TEST_END
