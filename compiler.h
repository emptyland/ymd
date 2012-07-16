#ifndef YMD_COMPILER_H
#define YMD_COMPILER_H

#include "lex.h"
#include "value.h"
#include <setjmp.h>
#include <stdio.h>

struct ymd_mach;

struct ymd_parser {
	struct ymd_lex lex;
	struct ytoken lah; // look a head;
	struct znode *lnk; // symbol strings
	struct chunk *blk; // top block
	struct loop_info *loop;
	int for_id; // for iterator id
	jmp_buf jpt;
	struct ymd_mach *vm;
};

// Compile to chunk object
struct chunk *ymc_compile(struct ymd_mach *vm, struct ymd_parser *p);

// Compile from source code buffer
struct func *ymd_compile(struct ymd_mach *vm, const char *name,
                         const char *fnam, const char *code);

// Compile from file stream
struct func *ymd_compilef(struct ymd_mach *vm, const char *name,
                          const char *fnam, FILE *fp);

#endif // YMD_COMPILER_H

