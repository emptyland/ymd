#ifndef YMD_DISASSEMBLY_H
#define YMD_DISASSEMBLY_H

#include "assembly.h"
#include "value.h"
#include <stdio.h>


int dis_inst(FILE *fp, const struct func *fn, uint_t inst);

int dis_func(FILE *fp, const struct func *fn);


#endif // YMD_DISASSEMBLY_H
