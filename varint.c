#include "varint.h"
#include <assert.h>

int varint16_encode(long long d, unsigned short rv[]) {
	unsigned long long coded = zigzag_encode(d);
	unsigned long long partal = 0;
	int i = 0;
	partal = coded & 0xffff000000000000ULL;
	if (partal) goto bits_48;
	partal = coded & 0x0000ffff00000000ULL;
	if (partal) goto bits_32;
	partal = coded & 0x00000000ffff0000ULL;
	if (partal) goto bits_16;
	partal = coded & 0x000000000000ffffULL;
	goto bits_00;
bits_48:
	rv[i++] = (unsigned short)(partal >> 48);
	partal = coded & 0x0000ffff00000000ULL;
bits_32:
	rv[i++] = (unsigned short)(partal >> 32);
	partal = coded & 0x00000000ffff0000ULL;
bits_16:
	rv[i++] = (unsigned short)(partal >> 16);
	partal = coded & 0x000000000000ffffULL;
bits_00:
	rv[i++] = (unsigned short)partal;
	return i;
}

long long varint16_decode(const unsigned short by[], int n) {
	int i = 0;
	unsigned long long raw = 0;
	switch (n) {
	case 1: goto bits_00;
	case 2: goto bits_16;
	case 3: goto bits_32;
	case 4: goto bits_48;
	default: assert(0); break;
	}
bits_48:
	raw |= ((unsigned long long)by[i++] << 48);
bits_32:
	raw |= ((unsigned long long)by[i++] << 32);
bits_16:
	raw |= ((unsigned long long)by[i++] << 16);
bits_00:
	raw |= by[i];
	return zigzag_decode(raw);
}

