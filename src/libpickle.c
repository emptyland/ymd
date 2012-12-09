#include "core.h"
#include "libc.h"
#include "pickle.h"

#define L struct ymd_context *l

static int libx_dump(L) {
	struct variable *arg0 = ymd_argv_get(l, 0);
	*ymd_push(l) = *arg0;
	if (ymd_pdump(l) == 0) {
		ymd_error(l, "pickle.pdump failed!");
		ymd_raise(l);
	}
	return 1;
}

static int libx_load(L) {
	struct kstr *arg0 = kstr_of(l, ymd_argv_get(l, 0));
	if (ymd_pload(l, arg0->land, arg0->len) == 0) {
		ymd_error(l, "pickle.load failed!");
		ymd_raise(l);
	}
	return 1;
}

LIBC_BEGIN(Pickle)
	LIBC_ENTRY(dump)
	LIBC_ENTRY(load)
LIBC_END

int ymd_load_pickle(struct ymd_mach *vm) {
	struct ymd_context *l = ioslate(vm);
	int i;
	ymd_hmap(l, 2);
	i = ymd_load_mem(l, "pickle", lbxPickle);
	ymd_putg(l, "pickle");
	return i;
}
