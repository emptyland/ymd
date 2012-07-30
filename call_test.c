#include "disassembly.h"
#include "core.h"
#include "libc.h"
#include "yut_rand.h"
#include "yut.h"

static int test_call_run_1() {
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(call_run_1, normal)
TEST_END
