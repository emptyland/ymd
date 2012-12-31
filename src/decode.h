#ifndef YMD_DISASSEMBLY_H
#define YMD_DISASSEMBLY_H

#include "builtin.h"
#include <stdio.h>

int dasm_inst(FILE *fp, const struct func *fn, uint_t inst);

int dasm_func(FILE *fp, const struct func *fn);


#endif // YMD_DISASSEMBLY_H
