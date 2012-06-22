#include "compiler.h"
#include "yut.h"

static int test_ymc_expr() {
	struct ymd_parser p;
	/*lex_init(&p.lex, NULL, "4 * (b + 2) / a\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "(4 * (i + 2)) + 4 / -c\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "a.i[0]\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "1 + n.a.i[0 + (1 + a.i)/b.n]\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "i + b.i(1, 2)\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "i + b.i(1, 2, f(f() + 1))\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "f([1,2,3,4])[1][2]\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "f({a:@{b:[]}})\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "a * 4 + b / 2\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "1 + print{a:1} / f()\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);*/
	lex_init(&p.lex, NULL, "env().a.i[1 + 2 * b] = f()\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "print(1,2,3)\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	lex_init(&p.lex, NULL, "print(1,2,3) = 2\n");
	printf("----:%s", p.lex.buf);
	ymc_compile(&p);
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(ymc_expr, normal)
TEST_END
