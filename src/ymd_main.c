#include "disassembly.h"
#include "core.h"
#include "compiler.h"
#include "libc.h"
#include "libtest.h"
#include "flags.h"
#include <stdio.h>
#include <string.h>

struct {
	int color;
	int dump;
	int test;
	int test_repeated;
	char test_filter[MAX_FLAG_STRING_LEN];
	char logf[MAX_FLAG_STRING_LEN];
} cmd_opt;

const struct ymd_flag_entry cmd_entries[] = {
	{
		"log_file",
		"Debug log file name.",
		cmd_opt.logf,
		FlagString,
	}, {
		"dump",
		"Dump bytecode only.",
		&cmd_opt.dump,
		FlagBool,
	}, {
		"test",
		"Run Yamada Unit Test?",
		&cmd_opt.test,
		FlagBool,
	}, {
		"repeated",
		"Number of yut test repeated.",
		&cmd_opt.test_repeated,
		FlagInt,
	}, {
		"color",
		"Output color option.",
		&cmd_opt.color,
		FlagTriBool,
	}, { NULL, NULL, NULL, NULL },
};

static void die(const char *info) {
	fprintf(stderr, "%s\n", info);
	exit(1);
}

int main(int argc, char *argv[]) {
	int i, argv_off = 0;
	struct ymd_mach *vm;
	struct ymd_context *l;
	FILE *input = NULL;
	const char *input_file;
	// Flags parsing:
	ymd_flags_parse(cmd_entries, NULL, &argc, &argv, 1);
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--argv") == 0) {
			argv_off = i + 1;
			break;
		} else {
			input_file = argv[i];
			input = fopen(input_file, "r");
			if (!input)
				die("Bad file!");
		}
	}
	vm = ymd_init();
	l = ioslate(vm);
	if (!input)
		die("No file input!");
	// Compile __main__ function
	if (ymd_compilef(l, "__main__", input_file, input) < 0)
		exit(1);
	ymd_load_lib(vm, lbxBuiltin);
	ymd_load_os(vm);
	ymd_load_pickle(vm);
	if (cmd_opt.test) ymd_load_ut(vm);
	// Dump all byte code only.
	if (cmd_opt.dump) {
		if (l->top > l->stk)
			dasm_func(stdout, func_k(ymd_top(l, 0)));
		return 0;
	}
	if (cmd_opt.test)
		i = ymd_test(l, argc - argv_off, argv + argv_off);
	else
		i = ymd_main(l, argc - argv_off, argv + argv_off);
	// Finalize:
	if (input) fclose(input);
	ymd_final(vm);
	return i;
}
