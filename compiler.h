#ifndef YMD_COMPILER_H
#define YMD_COMPILER_H

#include "lex.h"
#include <setjmp.h>

struct ymd_parser {
	struct ymd_lex lex;
	struct ytoken lah; // look a head;
	jmp_buf jpt;
};

void ymc_compile(struct ymd_parser *p);

#endif // YMD_COMPILER_H

