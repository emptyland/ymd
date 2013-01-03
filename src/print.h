#ifndef YMD_PRINT_H
#define YMD_PRINT_H

#include <stdio.h>

int ymd_set_colored(int on);

enum ymd_color_e {
	cEND,
	cRED,
	cGREEN,
	cYELLOW,
	cBLUE,
	cPURPLE,
	cAZURE,
	dRED,
	dGREEN,
	dYELLOW,
	dBLUE,
	dPURPLE,
	dAZURE,
	cMAX,
};

// Colored printf
// Example:
// Red color :   "${[red]The Red string}$"
// Green color : "${[green]The Green string}$"
int ymd_vfprintf(FILE *fp, const char *fmt, va_list ap);

#if defined(_MSC_VER)
int ymd_fprintf(FILE *fp, const char *fmt, ...);

int ymd_printf(const char *fmt, ...);
#else
int ymd_fprintf(FILE *fp, const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));

int ymd_printf(const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 1, 2)));
#endif

#endif // YMD_PRINT_H
