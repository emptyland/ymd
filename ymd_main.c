#include "disassembly.h"
#include "core.h"
#include "compiler.h"
#include "libc.h"
#include "libtest.h"
#include <stdio.h>
#include <string.h>

struct cmd_opt {
	FILE *input;
	FILE *gc_logf;
	const char *name;
	int external;
	int debug;
	int argv_off;
	int test;
};

static struct cmd_opt opt = {
	NULL,
	NULL,
	NULL,
	0,
	0,
	1,
	0,
};

static void die(const char *msg) {
	printf("Fatal: %s\n", msg);
	exit(0);
}

int main(int argc, char *argv[]) {
	int i;
	struct ymd_mach *vm;
	struct ymd_context *l;
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-d") == 0) {
			opt.debug = 1;
		} else if (strcmp(argv[i], "--argv") == 0) {
			opt.argv_off = i + 1;
			break;
		} else if (strcmp(argv[i], "--test") == 0) {
			opt.test = 1;
		} else if (strcmp(argv[i], "--gc-state") == 0) {
			opt.gc_logf = fopen("gc.log", "w");
		} else {
			opt.external = 1;
			opt.name = argv[i];
			opt.input = fopen(opt.name, "r");
			if (!opt.input)
				die("Bad file!");
		}
	}
	vm = ymd_init();
	l = ioslate(vm);
	if (opt.gc_logf)
		ymd_log4gc(vm, opt.gc_logf);
	if (!opt.input)
		die("Null file!");
	if (ymd_compilef(l, "__main__", opt.name, opt.input) < 0)
		exit(1);
	ymd_load_lib(vm, lbxBuiltin);
	ymd_load_os(vm);
	ymd_load_pickle(vm);
	if (opt.test) ymd_load_ut(vm);
	if (opt.debug)
		if (l->top > l->stk) dis_func(stdout, func_k(ymd_top(l, 0)));
	if (opt.test)
		i = ymd_test(l, argc - opt.argv_off, argv + opt.argv_off);
	else
		i = ymd_main(l, argc - opt.argv_off, argv + opt.argv_off);
	if (opt.external)
		fclose(opt.input);
	if (opt.gc_logf)
		fclose(opt.gc_logf);
	ymd_final(vm);
	return i;
}
