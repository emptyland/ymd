#ifndef YMD_ENCODE_H
#define YMD_ENCODE_H

#include "value.h"

// Encoding/Decoding
static inline ymd_uint_t zigzag_encode(ymd_int_t i) {
	if (i < 0)
		return (ymd_uint_t)(((-i) << 1) | 1ULL);
	return (ymd_uint_t)(i << 1);
}
static inline ymd_int_t zigzag_decode(ymd_uint_t u) {
	if (u & 0x1ULL)
		return -((ymd_int_t)(u >> 1));
	return (ymd_uint_t)(u >> 1);
}

#define MAX_VARINT16_LEN 4
int varint16_encode(ymd_int_t d, unsigned short rv[]);
ymd_int_t varint16_decode(const unsigned short by[], int n);

int stresc(const char *i, char **rv);
ymd_int_t xtoll(const char *raw);
ymd_int_t dtoll(const char *raw);

#endif // YMD_ENCODE_H

