#ifndef YMD_ENCODING_H
#define YMD_ENCODING_H

#include "value.h"
#include "builtin.h"

// Encoding/Decoding
static inline ymd_uint_t zigzag64encode(ymd_int_t i) {
	if (i < 0)
		return (ymd_uint_t)(((-i) << 1) | 1ULL);
	return (ymd_uint_t)(i << 1);
}
static inline ymd_int_t zigzag64decode(ymd_uint_t u) {
	if (u & 0x1ULL)
		return -((ymd_int_t)(u >> 1));
	return (ymd_uint_t)(u >> 1);
}
static inline ymd_u32_t zigzag32encode(ymd_i32_t i) {
	if (i < 0)
		return (ymd_u32_t)(((-i) << 1) | 1U);
	return (ymd_u32_t)(i << 1);
}
static inline ymd_i32_t zigzag32decode(ymd_u32_t u) {
	if (u & 0x1U)
		return -((ymd_i32_t)(u >> 1));
	return (ymd_u32_t)(u >> 1);
}

#define MAX_VARINT64_LEN 10
#define MAX_VARINT32_LEN  5

int stresc(const char *i, char **rv);
ymd_int_t xtoll(const char *raw, int *ok);
ymd_int_t dtoll(const char *raw, int *ok);

// Unsigned int encoding:
int uint64encode(ymd_uint_t x, ymd_byte_t *rv);
ymd_uint_t uint64decode(const ymd_byte_t *x, size_t *k);
int uint32encode(ymd_u32_t x, ymd_byte_t *rv);
ymd_u32_t uint32decode(const ymd_byte_t *x, size_t *k);

// Signed int encoding:
static inline int sint64encode(ymd_int_t x, ymd_byte_t *rv) {
	return uint64encode(zigzag64encode(x), rv);
}
static inline ymd_int_t sint64decode(const ymd_byte_t *x, size_t *k) {
	return zigzag64decode(uint64decode(x, k));
}
static inline int sint32encode(ymd_i32_t x, ymd_byte_t *rv) {
	return uint32encode(zigzag32encode(x), rv);
}
static inline ymd_i32_t sint32decode(const ymd_byte_t *x, size_t *k) {
	return zigzag32decode(uint32decode(x, k));
}

#endif // YMD_ENCODING_H

