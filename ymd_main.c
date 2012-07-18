#include "disassembly.h"
#include "value.h"
#include "memory.h"
#include "state.h"
#include "compiler.h"
#include "libc.h"
#include "libtest.h"
#include <stdio.h>
#include <string.h>

struct cmd_opt {
	FILE *input;
	const char *name;
	int external;
	int debug;
	int argv_off;
	int test;
};

static struct cmd_opt opt = {
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
	struct func *fn;
	struct ymd_mach *vm;
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-d") == 0) {
			opt.debug = 1;
		} else if (strcmp(argv[i], "--argv") == 0) {
			opt.argv_off = i + 1;
			break;
		} else if (strcmp(argv[i], "--test") == 0) {
			opt.test = 1;
		} else {
			opt.external = 1;
			opt.name = argv[i];
			opt.input = fopen(opt.name, "r");
			if (!opt.input)
				die("Bad file!");
		}
	}
	vm = ymd_init();
	if (!opt.input)
		die("Null file!");
	fn = ymd_compilef(vm, "__main__", opt.name, opt.input);
	if (!fn)
		exit(1);
	ymd_load_lib(vm, lbxBuiltin);
	if (opt.test) ymd_load_ut(vm);
	if (opt.debug) {
		int i;
		printf("====[main]====\n");
		dis_func(stdout, fn);
		for (i = 0; i < vm->n_fn; ++i) {
			printf("====[%s]====\n", vm->fn[i]->proto->land);
			dis_func(stdout, vm->fn[i]);
		}
	}
	if (opt.test)
		i =  ymd_test(vm, fn, argc - opt.argv_off, argv + opt.argv_off);
	else
		i = ymd_main(vm, fn, argc - opt.argv_off, argv + opt.argv_off);
	if (opt.external)
		fclose(opt.input);
	ymd_final(vm);
	return i;
}
