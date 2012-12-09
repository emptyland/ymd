#include "print.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

static int colored = 1;

int ymd_set_colored(int on) {
	int old = colored;
	colored = on;
	return old;
}

static int ymd_kesc(const char *z) {
	int rv;
	if (!*z || *z++ != '{')
		return -1;
	if (!*z || *z < 'a' || *z > 'm')
		return -1;
	rv = *z++ - 'a';
	if (!*z || *z != '}')
		return -1;
	return rv;
}

static const struct {
	size_t n;
	const char *z;
} kesc[] = {
#define ENTRY(z) { sizeof(z) - 1, z, }
	ENTRY("\033[0m"), // END
	ENTRY("\033[1;31m"), // Red
	ENTRY("\033[1;32m"), // Green
	ENTRY("\033[1;33m"), // Yellow
	ENTRY("\033[1;34m"), // Blue
	ENTRY("\033[1;35m"), // Purple
	ENTRY("\033[1;36m"), // Azure
	ENTRY("\033[0;31m"), // Dark Red
	ENTRY("\033[0;32m"), // Dark Green
	ENTRY("\033[0;33m"), // Dark Yellow
	ENTRY("\033[0;34m"), // Dark Blue
	ENTRY("\033[0;35m"), // Dark Purple
	ENTRY("\033[0;36m"), // Dark Azure
#undef ENTRY
};

static size_t ymd_kesc_do(int i, char *buf) {
	assert(i >= 0);
	assert(i < (int)(sizeof(kesc)/sizeof(kesc[0])));
	strcpy(buf, kesc[i].z);
	return kesc[i].n;
}

const char *ymd_print_paint(const char *priv) {
	int ki, i = 0;
	char *fmt;
	const char *p;
	size_t len = strlen(priv);
	if (len == 0)
		return strdup("");
	fmt = calloc(len * 2, 1);
	for (p = priv; *p; ++p) {
		switch (*p) {
		case '%':
			if ((ki = ymd_kesc(p + 1)) < 0)
				goto raw;
			p += 3; // skip "%{a}" string
			if (colored)
				i += ymd_kesc_do(ki, fmt + i);
			break;
		default:
		raw:
			fmt[i++] = *p;
			break;
		}
	}
	return fmt;
}

// Example:
// ymd_printf(yGREEN"[===]"yEND" %d", i);
int ymd_printf(const char *raw, ...) {
	va_list ap;
	int rv;
	const char *fmt = ymd_print_paint(raw);
	va_start(ap, raw);
	rv = vprintf(fmt, ap);
	va_end(ap);
	free((char *)fmt);
	return rv;
}
