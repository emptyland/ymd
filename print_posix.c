#include "print.h"
#include <stdio.h>

static int colored = 1;

int ymd_set_colored(int on) {
	int old = colored;
	colored = on;
	return old;
}

// Example:
// ymd_print(yGREEN"[===]"yEND" %d", i);
int ymd_printf(const char *fmt, ...) {
	(void)fmt;
	return 0;
}
