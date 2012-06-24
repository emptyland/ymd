#ifndef YMD_COMPILER_H
#define YMD_COMPILER_H

#include "lex.h"
#include "value.h"
#include <setjmp.h>
#include <stdio.h>

struct ymd_parser {
	struct ymd_lex lex;
	struct ytoken lah; // look a head;
	struct znode *lnk; // symbol strings
	struct chunk *blk; // top block
	struct loop_info *loop;
	int for_id; // for iterator id
	jmp_buf jpt;
};

struct chunk *ymc_compile(struct ymd_parser *p);
struct func *func_compile(const char *name, const char *fnam,
                          const char *code);
struct func *func_compilef(const char *name, const char *fnam,
                           FILE *fp);

#endif // YMD_COMPILER_H

