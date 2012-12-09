#ifndef YMD_PICKLE_H
#define YMD_PICKLE_H

#include "builtin.h"

struct zistream;
struct zostream;

int ymd_dump_kstr(struct zostream *os, const struct kstr *kz);
int ymd_dump_dyay(struct zostream *os, const struct dyay *ax, int *ok);
int ymd_dump_hmap(struct zostream *os, const struct hmap *mx, int *ok);
int ymd_dump_skls(struct zostream *os, const struct skls *sk, int *ok);
int ymd_dump_chunk(struct zostream *os, const struct chunk *bk, int *ok);
int ymd_dump_func(struct zostream *os, const struct func *fn, int *ok);
int ymd_serialize(struct zostream *os, const struct variable *v,int *ok);

// dump variable from top
int ymd_pdump(struct ymd_context *l);

int ymd_load_kstr(struct zistream *is, int *ok);
int ymd_load_dyay(struct zistream *is, int *ok);
int ymd_load_chunk(struct zistream *is, struct chunk *x, int *ok);
int ymd_load_func(struct zistream *is, int *ok);
int ymd_load_hmap(struct zistream *is, int *ok);
int ymd_load_skls(struct zistream *is, int *ok);
int ymd_parse(struct zistream *is, int *ok);

// load variable to top
int ymd_pload(struct ymd_context *l, const void *z, int k);

#endif // YMD_PICKLE_H
