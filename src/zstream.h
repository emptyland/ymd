#ifndef YMD_ZSTREAM_H
#define YMD_ZSTREAM_H

#include "builtin.h"
#include "encoding.h"
#include <assert.h>
#include <string.h>

#define context(s) ((s)->l)

// Zero Copy Input Stream
struct zistream {
	const ymd_byte_t *buf;
	int last;
	int max;
	struct ymd_context *l;
	const char *err;
};

// Zero Copy Output Stream
#define MAX_STATIC_LEN 4096
struct zostream {
	ymd_byte_t kbuf[MAX_STATIC_LEN];
	ymd_byte_t *buf;
	int last;
	int max;
	const char *err;
};

//------------------------------------------------------------------------
// zistream:
//------------------------------------------------------------------------
#define ZIS_INIT(l, z, k) { z, 0, k, l, NULL, }
#define ZIS struct zistream *is

static YMD_INLINE void zis_final(ZIS) {
	is->last = 0;
	is->err = NULL;
}

static YMD_INLINE void zis_reset(ZIS, const void *z, int k) {
	zis_final(is);
	is->buf = z;
	is->max = k;
}

static YMD_INLINE void zis_pipe(ZIS, const struct zostream *os) {
	zis_reset(is, os->buf ? os->buf : os->kbuf, os->last);
}

static YMD_INLINE int zis_remain(ZIS) {
	return is->max - is->last;
}

static YMD_INLINE const void *zis_last(ZIS) {
	return is->buf + is->last;
}

static YMD_INLINE const void *zis_advance(ZIS, int k) {
	assert(zis_remain(is) >= k);
	is->last += k;
	return zis_last(is);
}

static YMD_INLINE const void *zis_fetch(ZIS, void *z, int k) {
	assert(zis_remain(is) >= k);
	memcpy(z, zis_last(is), k);
	return zis_advance(is, k);
}

static YMD_INLINE ymd_u32_t zis_u32(ZIS) {
	size_t k = 0;
	ymd_u32_t x = uint32decode(zis_last(is), &k);
	zis_advance(is, (int)k);
	return x;
}

static YMD_INLINE ymd_uint_t zis_u64(ZIS) {
	size_t k = 0;
	ymd_uint_t x = uint64decode(zis_last(is), &k);
	zis_advance(is, (int)k);
	return x;
}

static YMD_INLINE ymd_int_t zis_i64(ZIS) {
	size_t k = 0;
	ymd_int_t x = sint64decode(zis_last(is), &k);
	zis_advance(is, (int)k);
	return x;
}

static YMD_INLINE ymd_float_t zis_float(ZIS) {
	ymd_float_t x;
	memcpy(&x, zis_last(is), sizeof(x));
	zis_advance(is, (int)sizeof(x));
	return x;
}

#undef ZIS
//------------------------------------------------------------------------
// zostream:
//------------------------------------------------------------------------
#define ZOS_INIT { {0}, NULL, 0, MAX_STATIC_LEN, NULL, }
#define ZOS struct zostream *os

static YMD_INLINE void zos_final(ZOS) {
	if (os->buf) free(os->buf);
	memset(os->kbuf, 0, MAX_STATIC_LEN);
	os->buf = NULL, os->last = 0, os->max = MAX_STATIC_LEN;
}

static YMD_INLINE void *zos_buf(ZOS) {
	return os->buf ? os->buf : os->kbuf;
}

static YMD_INLINE void *zos_last(ZOS) {
	return (ymd_byte_t*)zos_buf(os) + os->last;
}

static YMD_INLINE void zos_reserved(ZOS, int k) {
	if (os->last + k <= MAX_STATIC_LEN) return;
	if (os->last + k <= os->max) return;
	os->max <<= 1;
	if (!os->buf) {
		os->buf = calloc(os->max, 1);
		if (os->last > 0) memcpy(os->buf, os->kbuf, os->last);
	} else
		os->buf = realloc(os->buf, os->max);
}

static YMD_INLINE int zos_remain(ZOS) {
	return os->max - os->last;
}

static YMD_INLINE void *zos_advance(ZOS, int k) {
	zos_reserved(os, k);
	os->last += k;
	return zos_last(os);
}

static YMD_INLINE void *zos_append(ZOS, const void *z, int k) {
	zos_reserved(os, k);
	memcpy(zos_last(os), z, k);
	return zos_advance(os, k);
}

static YMD_INLINE int zos_u32(ZOS, ymd_u32_t x) {
	int i = 0;
	zos_reserved(os, MAX_VARINT32_LEN);
	i = uint32encode(x, zos_last(os));
	zos_advance(os, i);
	return i;
}

static YMD_INLINE int zos_u64(ZOS, ymd_uint_t x) {
	int i = 0;
	zos_reserved(os, MAX_VARINT64_LEN);
	i = uint64encode(x, zos_last(os));
	zos_advance(os, i);
	return i;
}

static YMD_INLINE int zos_i64(ZOS, ymd_int_t x) {
	int i = 0;
	zos_reserved(os, MAX_VARINT64_LEN);
	i = sint64encode(x, zos_last(os));
	zos_advance(os, i);
	return i;
}

static YMD_INLINE int zos_float(ZOS, ymd_float_t x) {
	zos_append(os, &x, sizeof(x));
	return sizeof(x);
}

#undef ZOS

#endif // YMD_ZSTREAM_H
