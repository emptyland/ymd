#ifndef YMD_SYMBOL_H
#define YMD_SYMBOL_H

#include "assembly.h"

struct func;

// Symbol functions:
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

// Emiting functions:
void emit_access(unsigned char op, const char *z);
void emit_bind(const char *z);

// Scope functions:
int sop_push(struct func *fn);
struct func *sop_pop();
struct func *sop_index(int i);
int sop_count();
int sop_result();
void sop_error(const char *err, int rv);
// Get last scope func:
static inline struct func *sop() { return sop_index(-1); }
// Function frame adjust:
void sop_adjust(struct func *fn);

// Compiling Information functions:
void info_loop_push(int pos);
ushort_t info_loop_off(const struct func *fn);
void info_loop_back(struct func *fn, int death);
void info_loop_pop();
void info_loop_rcd(char flag, int pos);
void info_loop_set_retry(int pos);
void info_loop_set_jcond(int pos);
const char *info_loop_iter_name();
void info_loop_set_id(const char *id);
const char *info_loop_id();
// Condition information
void info_cond_push(int pos);
void info_cond_back(struct func *fn);
void info_cond_pop();
void info_cond_rcd(char which, int pos);

// Converation
int stresc(const char *raw, char **rv);
long long xtoll(const char *raw);
long long dtoll(const char *raw);

#endif // YMD_SYMBOL_H
