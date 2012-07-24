#ifndef YMD_LIBTEST_H
#define YMD_LIBTEST_H

struct func;
struct ymd_mach;

// Load unit test library
int ymd_load_ut(struct ymd_mach *vm);

// Run all test
// Run chunk as a unit test
// Example:
// Test = @{}
// func Test.init() { ... }      // optional
// func Test.final() { ... }     // optional
// func Test.setup () { ... }    // optional
// func Test.teardown () { ... } // optional
// func Test.test0 () { ... }
// func Test.test1 () { ... }
//
// ymd_test() can call Test.test0 and Test.test1 as 2 test case.
// the test must contain literal: "Test" as a `hashmap` or `skiplist`;
// the case must contain literal: "test" as a `func`
int ymd_test(struct ymd_mach *vm, struct func *fn, int argc, char *argv[]);

#endif // YMD_LIBTEST_H
