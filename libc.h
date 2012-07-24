#ifndef YMD_LIBC_H
#define YMD_LIBC_H

#include "value.h"

struct ymd_mach;
//--------------------------------------------------------------------------
// Stream define
//--------------------------------------------------------------------------
struct yio_stream {
	ymd_nafn_t read;
	ymd_nafn_t write;
};

struct libfn_symbol {
	int len;
	const char *z;
};

struct libfn_entry {
	struct libfn_symbol symbol;
	ymd_nafn_t native;
};

typedef const struct libfn_entry ymd_libc_t[];

#define LIBC_BEGIN(name) \
	const struct libfn_entry lbx##name[] = {
#define LIBC_ENTRY(fn) \
		{ { sizeof(#fn) - 1, #fn }, libx_##fn, },
#define LIBC_END \
		{ { 0, NULL, }, NULL, }, \
	};

extern ymd_libc_t lbxBuiltin;

int ymd_load_lib(struct ymd_mach *vm, ymd_libc_t lbx);

// Load methods of object
int ymd_load_mem(struct ymd_context *l, const char *clazz, ymd_libc_t lbx);

// Load os library
int ymd_load_os(struct ymd_mach *vm);

#endif // YMD_LIBC_H

