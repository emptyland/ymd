#include "disassembly.h"
#include "value.h"
#include "memory.h"
#include "state.h"
#include "libc.h"
#include <stdio.h>
#include <string.h>

struct cmd_opt {
	FILE *input;
	int external;
	int debug;
	int argv_off;
};

static struct cmd_opt opt = {
	NULL,
	0,
	0,
	1,
};

static void die(const char *msg) {
	printf("Fatal: %s\n", msg);
	exit(0);
}

int main(int argc, char *argv[]) {
	int i;
	struct func *fn;
	opt.input = stdin;
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-d") == 0) {
			opt.debug = 1;
		} else if (strcmp(argv[i], "--argv") == 0) {
			opt.argv_off = i + 1;
			break;
		} else {
			opt.external = 1;
			opt.input = fopen(argv[i], "r");
			if (!opt.input)
				die("Bad file!");
		}
	}
	vm_init();
	vm_init_context();
	fn = func_compile(opt.input);
	if (!fn)
		die("Syntax error!");
	ymd_load_lib(lbxBuiltin);
	if (opt.debug) {
		int i;
		printf("====[main]====\n");
		dis_func(stdout, fn);
		for (i = 0; i < vm()->n_fn; ++i) {
			printf("====[%s]====\n", vm()->fn[i]->proto->land);
			dis_func(stdout, vm()->fn[i]);
		}
	}
	//func_call(fn, 0);
	i = func_main(fn, argc - opt.argv_off, argv + opt.argv_off);
	if (opt.external)
		fclose(opt.input);
	vm_final_context();
	vm_final();
	return i;
}
