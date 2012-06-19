#include "compiler.h"
#include "yut.h"

static int test_ymc_expr() {
	struct ymd_parser p;
	lex_init(&p.lex, NULL, "4 * (1 + 2) / 4\n");
	ymc_compile(&p);
	printf("---------\n");
	lex_init(&p.lex, NULL, "(4 * (1 + 2)) + 4 / -4\n");
	ymc_compile(&p);
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(ymc_expr, normal)
TEST_END
