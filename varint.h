#ifndef YMD_VARINT_H
#define YMD_VARINT_H

// Encoding/Decoding
static inline unsigned long long zigzag_encode(long long i) {
	if (i < 0)
		return (unsigned long long)(((-i) << 1) | 1ULL);
	return (unsigned long long)(i << 1);
}
static inline long long zigzag_decode(unsigned long long u) {
	if (u & 0x1ULL)
		return -((long long)(u >> 1));
	return (unsigned long long)(u >> 1);
}
#define MAX_VARINT16_LEN 4
int varint16_encode(long long d, unsigned short rv[]);
long long varint16_decode(const unsigned short by[], int n);

#endif // YMD_VARINT_H
