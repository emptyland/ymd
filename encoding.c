#include "encoding.h"
#include <assert.h>

//------------------------------------------------------------------------------
// Symbol -> Integer -> String
//------------------------------------------------------------------------------
static inline ymd_int_t xtol(char x, int *ok) {
	if (x >= '0' && x <= '9')
		return x - '0';
	else if (x >= 'a' && x <= 'f')
		return 10 + x - 'a';
	else if (x >= 'A' && x <= 'F')
		return 10 + x - 'A';
	*ok = 0;
	return 0;
}

static inline ymd_int_t dtol(char x, int *ok) {
	if (x >= '0' && x <= '9')
		return x - '0';
	*ok = 0;
	return 0;
}

static inline void *priv_need(void *raw, int n, int align,
                              size_t chunk) {
	char *rv;
	if (n % align)
		return raw;
	rv = realloc(raw, chunk * (n + align));
	memset(rv + chunk * n, 0, chunk * align);
	return rv;
}

static inline char *priv_add(char *priv, int n, char c) {
	priv = priv_need(priv, n, 64, 1);
	priv[n] = c;
	return priv;
}

#define ESC_CHAR(escchar, c) \
	case escchar: \
		priv = priv_add(priv, n++, c); \
		break
#define ESC_BYTE() \
	c = (char)xtol(*i++, &ok) << 4; \
	if (!ok) goto error; \
	c |= (char)xtol(*i++, &ok); \
	if (!ok) goto error; \
	priv = priv_add(priv, n++, c)
int stresc(const char *i, char **rv) {
	char c, *priv = NULL;
	int ok = 1, n = 0;
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
			error:
			default:
				if (priv) free(priv);
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

ymd_int_t xtoll(const char *raw, int *ok) {
	ymd_int_t rv = 0;
	int k = strlen(raw), i = k;
	assert(k > 2);
	assert(raw[0] == '0');
	assert(raw[1] == 'x');
	while (i-- > 2) {
		rv |= (xtol(raw[i], ok) << ((k - i - 1) * 4));
		if (!*ok)
			return 0LL;
	}
	return rv;
}

ymd_int_t dtoll(const char *raw, int *ok) {
	ymd_int_t rv = 0;
	ymd_int_t pow = 1;
	int k = strlen(raw), i = k;
	if (k > 1 && raw[0] == '-')
		k = 1;
	else
		k = 0;
	while (i-- > k) {
		rv += (dtol(raw[i], ok) * pow);
		if (!*ok)
			return 0LL;
		pow *= 10;
	}
	return k ? -rv : rv;
}

#define TEST_OR_SET(c) \
	by = ((x & 0x7fULL << (c*7)) >> (c*7))
#define FILL_AND_TEST_OR_SET(c) \
	rv[i++] = (by | 0x80); \
	TEST_OR_SET(c)
int uint64encode(ymd_uint_t x, ymd_byte_t *rv) {
	int i = 0;
	ymd_byte_t by;
	//       [+---+---+---+---]
	if (x & 0xFFFE000000000000ULL) {
		if ((by = ((x & 0x1ULL << 63) >> 63)) != 0) // 64
			goto bit_63_k0;
		if ((TEST_OR_SET(8)) != 0) // 58~63
			goto bit_56_k1;
		if ((TEST_OR_SET(7)) != 0) // 50~57
			goto bit_49_k2;
	}
	//      [-+---+---+---]
	if (x & 0x1FFFFF0000000ULL) {
		if ((TEST_OR_SET(6)) != 0) // 43~49
			goto bit_42_k3;
		if ((TEST_OR_SET(5)) != 0) // 36~42
			goto bit_35_k4;
		if ((TEST_OR_SET(4)) != 0) // 29~35
			goto bit_28_k5;
	}
	//       [---+---]
	if (x & 0xFFFFF80ULL) {
		if ((TEST_OR_SET(3)) != 0) // 22~28
			goto bit_21_k6;
		if ((TEST_OR_SET(2)) != 0) // 15~21
			goto bit_14_k7;
		if ((TEST_OR_SET(1)) != 0) // 8~14
			goto bit_07_k8;
	}
	TEST_OR_SET(0);
	rv[i++] = by;
	return i;
bit_63_k0:
	FILL_AND_TEST_OR_SET(8);
bit_56_k1:
	FILL_AND_TEST_OR_SET(7);
bit_49_k2:
	FILL_AND_TEST_OR_SET(6);
bit_42_k3:
	FILL_AND_TEST_OR_SET(5);
bit_35_k4:
	FILL_AND_TEST_OR_SET(4);
bit_28_k5:
	FILL_AND_TEST_OR_SET(3);
bit_21_k6:
	FILL_AND_TEST_OR_SET(2);
bit_14_k7:
	FILL_AND_TEST_OR_SET(1);
bit_07_k8:
	FILL_AND_TEST_OR_SET(0);
	rv[i++] = by;
	return i;
}
#undef TEST_OR_SET
#undef FILL_AND_TEST_OR_SET

ymd_uint_t uint64decode(const ymd_byte_t *rv, size_t *k) {
	size_t i = 0;
	ymd_byte_t by;
	ymd_uint_t x = 0;
	while ((by = rv[i++]) >= 0x80) {
		x |= (by & 0x7f);
		x <<= 7;
	}
	x |= by; // last 7bit
	*k = i;
	return x;
}

#define TEST_OR_SET(c) \
	by = ((x & 0x7FU << (c*7)) >> (c*7))
#define FILL_AND_TEST_OR_SET(c) \
	rv[i++] = (by | 0x80); \
	TEST_OR_SET(c)
int uint32encode(ymd_u32_t x, ymd_byte_t *rv) {
	int i = 0;
	ymd_byte_t by;
	if ((by = ((x & 0xFU << 28) >> 28)) != 0) // 29~32
		goto bit_28_k5;
	if ((TEST_OR_SET(3)) != 0) // 22~28
		goto bit_21_k6;
	if ((TEST_OR_SET(2)) != 0) // 15~21
		goto bit_14_k7;
	if ((TEST_OR_SET(1)) != 0) // 8~14
		goto bit_07_k8;
	TEST_OR_SET(0);
	rv[i++] = by;
	return i;
bit_28_k5:
	FILL_AND_TEST_OR_SET(3);
bit_21_k6:
	FILL_AND_TEST_OR_SET(2);
bit_14_k7:
	FILL_AND_TEST_OR_SET(1);
bit_07_k8:
	FILL_AND_TEST_OR_SET(0);
	rv[i++] = by;
	return i;
}

ymd_u32_t uint32decode(const ymd_byte_t *x, size_t *k) {
	return (ymd_u32_t)uint64decode(x, k);
}
