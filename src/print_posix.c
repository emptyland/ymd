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

static int startswith(const char *z, const char *prefix) {
	while (*prefix) {
		if (*z++ != *prefix++)
			return 0;
	}
	return 1;
}


/*
enum ymd_color_e {
	0  cEND, 
	1  cRED,
	2  cGREEN,
	3  cYELLOW,
	4  cBLUE,
	5  cPURPLE,
	6  cAZURE,
	7  dRED,
	8  dGREEN,
	9  dYELLOW,
	10 dBLUE,
	11 dPURPLE,
	12 dAZURE,
};
*/
static const char *lookup_escape(const char *z, int dark) {
	int index = 0;
	switch (*z) {
	case 'e': case 'E':
		index = 0;
		break;
	case 'r': case 'R':
		index = 1;
		break;
	case 'g': case 'G':
		index = 2;
		break;
	case 'y': case 'Y':
		index = 3;
		break;
	case 'b': case 'B':
		index = 4;
		break;
	case 'p': case 'P':
		index = 5;
		break;
	case 'a': case 'A':
		index = 6;
		break;
	default:
		return NULL;
	}
	if (dark) index += 6;
	return kesc[index].z;
}

static const char *colored_escape(const char *p) {
	size_t len = strlen(p);
	const char *k = p + len;
	char *rv, *z;
	if (!len)
		return strdup("");
	rv = calloc(len * 3, 1);
	for (z = rv; p < k; ++p) {
		if (startswith(p, "%{[")) {
			const char *esc = NULL;
			if (p[3] == '!') // Is %{[!red] prefix ?
				esc = lookup_escape(p + 4, 1);
			else
				esc = lookup_escape(p + 3, 0);
			if (esc) {
				while (*esc)
					*z++ = *esc++;
				while (*p != ']') // Skip %{[red] prefix
					p++;
				continue;
			}
		}
		if (startswith(p, "}%")) {
			const char *esc = lookup_escape("end", 0);
			if (esc) {
				while (*esc)
					*z++ = *esc++;
				p += 1; // Skip "%}"
				continue;
			}
		}
		*z++ = *p;
	}
	return rv;
}

// "%{[!red] Read }%"
int ymd_vfprintf(FILE *fp, const char *fmt, va_list ap) {
	const char *rfmt = colored_escape(fmt);
	int rv = vfprintf(fp, rfmt, ap);
	free((void *)rfmt);
	return rv;
}

int ymd_fprintf(FILE *fp, const char *fmt, ... ) {
	int rv;
	va_list ap;

	va_start(ap, fmt);
	rv = ymd_vfprintf(fp, fmt, ap);
	va_end(ap);
	return rv;
}

int ymd_printf(const char *fmt, ...) {
	int rv;
	va_list ap;

	va_start(ap, fmt);
	rv = ymd_vfprintf(stdout, fmt, ap);
	va_end(ap);
	return rv;

}
