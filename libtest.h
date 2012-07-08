#ifndef YMD_LIBTEST_H
#define YMD_LIBTEST_H

struct func;

// Load unit test library
int ymd_load_ut();

// Run all test
int ymd_test(struct func *fn, int argc, char *argv[]);

#endif // YMD_LIBTEST_H
