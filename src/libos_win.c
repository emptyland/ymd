#include "libc.h"
#include "core.h"
#include <windows.h>

#define L struct ymd_context *l

// upval[0] dir pattern
// upval[1] handle
static int readdir_iter(L) {
	WIN32_FIND_DATAA ffd;
	struct variable *ff = ymd_upval(l, 1);
	if (ff->u.ext == INVALID_HANDLE_VALUE) {
		char name[MAX_PATH];
		struct kstr *up0 = kstr_of(l, ymd_upval(l, 0));
		strncpy_s(name, ARRAY_SIZEOF(name), up0->land, up0->len);
		if (name[strlen(name) - 1] != '*')
			strcat_s(name, sizeof(name), "\\*");

		ff->u.ext = FindFirstFileA(name, &ffd);
		if (ff->u.ext == INVALID_HANDLE_VALUE)
			return 0;
		ymd_kstr(l, ffd.cFileName, -1);
		return 1;
	}
	if (!FindNextFileA(ff->u.ext, &ffd))
		return 0;
	ymd_kstr(l, ffd.cFileName, -1);
	return 1;
}

static int libx_readdir(L) {
	if (ymd_type(ymd_argv(l, 0)) != T_KSTR)
		ymd_panic(l,"readdir() : Bad arg0, need a string.");
	ymd_nafn(l, readdir_iter, "__readdir_iter__", 2);

	setv_kstr(ymd_push(l), kstr_of(l, ymd_argv(l, 0)));
	ymd_bind(l, 0); // bind dir name pattern

	ymd_ext(l, INVALID_HANDLE_VALUE);
	ymd_bind(l, 1); // bind findfile handle
	return 1;
}

static int libx_remove(L) {
	const char *name = kstr_of(l, ymd_argv(l, 0))->land;
	ymd_int(l, DeleteFileA(name));
	return 1;
}

LIBC_BEGIN(OSPosix)
	LIBC_ENTRY(readdir)
	//LIBC_ENTRY(fork)
	LIBC_ENTRY(remove)
	//LIBC_ENTRY(mkdir)
	//LIBC_ENTRY(stat)
LIBC_END

int ymd_load_os(struct ymd_mach *vm) {
	struct ymd_context *l = ioslate(vm);
	ymd_hmap(l, 0);
	ymd_kstr(l, "windows", -1);
	ymd_def(l, "platform");
	ymd_kstr(l, "0.1", -1);
	ymd_def(l, "version");
	ymd_load_mem(l, "os", lbxOSPosix);
	ymd_putg(l, "os");
	return 0;
}
