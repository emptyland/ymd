#ifndef YMD_COMPILER_H
#define YMD_COMPILER_H

#include "lex.h"
#include "value.h"
#include <setjmp.h>
#include <stdio.h>

struct ymd_mach;
struct func_env;

struct ymd_parser {
	struct ymd_lex lex;
	struct ytoken lah; // look a head;
	struct znode *lnk; // symbol strings
	struct func_env *env; // top block
	struct loop_info *loop;
	int for_id; // for iterator id
	jmp_buf jpt;
	struct ymd_mach *vm;
};

// Compile to chunk object
int ymc_compile(struct ymd_parser *p, struct chunk *blk);

// Compile from source code buffer
int ymd_compile(struct ymd_context *l, const char *name,
                const char *file, const char *code);

// Compile from file stream
int ymd_compilef(struct ymd_context *l, const char *name,
                 const char *file, FILE *fp);

#endif // YMD_COMPILER_H

