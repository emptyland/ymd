#include "print.h"
#include <windows.h>

static WORD win_color_attr[cMAX] = {
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,

	FOREGROUND_RED | FOREGROUND_INTENSITY,
	FOREGROUND_GREEN | FOREGROUND_INTENSITY,
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
	FOREGROUND_BLUE | FOREGROUND_INTENSITY,
	FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
	FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,

	FOREGROUND_RED,
	FOREGROUND_GREEN,
	FOREGROUND_RED | FOREGROUND_GREEN,
	FOREGROUND_BLUE,
	FOREGROUND_RED | FOREGROUND_BLUE,
	FOREGROUND_GREEN | FOREGROUND_BLUE,
};

static int startswith(const char *z, const char *prefix) {
	while (*prefix) {
		if (*z++ != *prefix++)
			return 0;
	}
	return 1;
}

static int set_color(int ss, WORD attr) {
	HANDLE handle = GetStdHandle((DWORD)ss);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	return SetConsoleTextAttribute(handle, attr);
}

static int paint(int ss, const char *z, int dark) {
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
		return 0;
	}
	if (dark) index += 6;
	return set_color(ss, win_color_attr[index]);
}

static void colored_escape(FILE *fp, const char *p) {
	size_t len = strlen(p);
	int ss = -10 - _fileno(fp);
	const char *k = p + len;
	if (!len)
		return;
	for (; p < k; ++p) {
		if (startswith(p, "${[")) {
			int esc = 0;
			if (p[3] == '!') // Is ${[!red] prefix ?
				esc = paint(ss, p + 4, 1);
			else
				esc = paint(ss, p + 3, 0);
			if (esc) {
				while (*p != ']') // Skip ${[red] prefix
					p++;
				continue;
			}
		}
		if (startswith(p, "}$")) {
			int esc = paint(ss, "end", 0);
			if (esc) {
				p += 1; // Skip "$}"
				continue;
			}
		}
		putc(*p, fp);
	}
}

int ymd_vfprintf(FILE *fp, const char *fmt, va_list ap) {
	char *buf;
	int rv;
	size_t len = strlen(fmt);
	if (!len)
		return 0;
	len = len * 3 + 1;
	buf = (char *)malloc(len);
	while ((rv = vsnprintf(buf, len, fmt, ap)) < 0) {
		len <<= 1;
		buf = (char *)realloc(buf, len);
	}
	colored_escape(fp, buf);
	free(buf);
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

