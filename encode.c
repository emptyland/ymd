#include "encode.h"
#include "state.h"
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

//------------------------------------------------------------------------------
// Symbol -> Integer -> String
//------------------------------------------------------------------------------
static inline ymd_int_t xtol(char x) {
	if (x >= '0' && x <= '9')
		return x - '0';
	else if (x >= 'a' && x <= 'f')
		return 10 + x - 'a';
	else if (x >= 'A' && x <= 'F')
		return 10 + x - 'A';
	assert(0);
	return -1;
}

static inline ymd_int_t dtol(char x) {
	if (x >= '0' && x <= '9')
		return x - '0';
	assert(0);
	return -1;
}

static inline char *priv_add(char *priv, int n, char c) {
	priv = mm_need(priv, n, 64, 1);
	priv[n] = c;
	return priv;
}

#define ESC_CHAR(escchar, c) \
	case escchar: \
		priv = priv_add(priv, n++, c); \
		break
#define ESC_BYTE() \
	c = (char)xtol(*i++) << 4; \
	c |= (char)xtol(*i++); \
	priv = priv_add(priv, n++, c)
int stresc(const char *i, char **rv) {
	char c, *priv = NULL;
	int n = 0;
	while (*i) {
		if (*i++ == '\\') {
			switch (*i++) {
			ESC_CHAR('a', '\a');
			ESC_CHAR('b', '\b');
			ESC_CHAR('f', '\f');
			ESC_CHAR('n', '\n');
			ESC_CHAR('r', '\r');
			ESC_CHAR('t', '\t');
			ESC_CHAR('v', '\v');
			ESC_CHAR('0', '\0');
			ESC_CHAR('\\', '\\');
			ESC_CHAR('\"', '\"');
			case 'x':
				ESC_BYTE();
				break;
			case 'u':
				ESC_BYTE();
				ESC_BYTE();
				break;
			default:
				vm_free(priv);
				return -1;
			}
			continue;
		}
		priv = priv_add(priv, n++, *(i - 1));
	}
	*rv = priv;
	return n;
}
#undef ESC_BYTE
#undef ESC_CHAR

ymd_int_t xtoll(const char *raw) {
	ymd_int_t rv = 0;
	int k = strlen(raw), i = k;
	assert(k > 2);
	assert(raw[0] == '0');
	assert(raw[1] == 'x');
	while (i-- > 2)
		rv |= (xtol(raw[i]) << ((k - i - 1) * 4));
	return rv;
}

ymd_int_t dtoll(const char *raw) {
	ymd_int_t rv = 0;
	ymd_int_t pow = 1;
	int k = strlen(raw), i = k;
	if (k > 1 && raw[0] == '-')
		k = 1;
	else
		k = 0;
	while (i-- > k) {
		rv += (dtol(raw[i]) * pow);
		pow *= 10;
	}
	return k ? -rv : rv;
}

