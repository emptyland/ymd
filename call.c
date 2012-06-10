#include "value.h"
#include "state.h"
#include "memory.h"
#include <stdio.h>

extern int do_compile(FILE *fp, struct func *fn);

static inline struct variable *func_local(struct func *fn, int i) {
	struct context *l = ioslate();
	assert(fn == l->info->run);
	return l->info->loc + i;
}

int vm_run(struct func *fn, int argc) {
	(void)fn;
	(void)argc;
	return 0;
}

int func_call(struct func *fn, int argc) {
	int rv;
	struct call_info scope;
	struct context *l = ioslate();

	// Link the call info
	scope.pc = 0;
	if (!l->info)
		scope.loc = l->loc;
	else
		scope.loc = l->info->loc + fn->n_lz;
	scope.run = fn;
	scope.chain = l->info;
	l->info = &scope;

	// Copy args
	if (fn->kargs <= argc) {
		int i = fn->kargs;
		while (i--)
			*func_local(fn, fn->kargs - i - 1) = *ymd_top(l, i);
	}
	if (fn->nafn) {
		rv = (*fn->nafn)(ioslate());
		goto ret;
	}
	rv = vm_run(fn, argc);
ret:
	ioslate()->info = ioslate()->info->chain;
	return rv;
}

struct func *func_compile(FILE *fp) {
	struct func *fn = func_new(NULL);
	int rv = do_compile(fp, fn);
	return rv < 0 ? NULL : fn;
}
