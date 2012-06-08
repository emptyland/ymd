#ifndef YMD_SYMBOL_H
#define YMD_SYMBOL_H

#include "assembly.h"

struct func;

// Symbol function:
int sym_count();
int sym_push(const char *z, int n);
const char *sym_index(int i);
void sym_pop();
void sym_scope_begin();
void sym_scope_end();
void sym_slot_begin();
void sym_slot_end();
int sym_slot(int i, int op);
int sym_last_slot(int i, int lv);

// Offset functions:
int push_off(unsigned short off);
unsigned short pop_off();
unsigned short index_off(int i);


// Scope functions:
int sop_push(struct func *fn);
struct func *sop_pop();
struct func *sop_index(int i);
int sop_count();
int sop_result();
void sop_error(const char *err, int rv);
void sop_fillback(int dict);
// Get last scope func:
static inline struct func *sop() { return sop_index(-1); }

// Compiling Information functions:
void info_loop_push(ushort_t pos);
ushort_t info_loop_off(const struct func *fn);
void info_loop_back(struct func *fn, int death);
void info_loop_pop();
void info_loop_rcd(char flag, ushort_t pos);

void info_cond_push(ushort_t pos);
void info_cond_back(struct func *fn);
void info_cond_pop();
void info_cond_rcd(char which, ushort_t pos);

#endif // YMD_SYMBOL_H
