#include "print.h"
#include "yut_rand.h"
#include "testing/print_test.def"

static int test_escape_color (void *p) {
	(void)p;
	ymd_fprintf(stdout, "${[!green]Green}$\n");
	ymd_fprintf(stdout, "${[green]Green}$\n");

	ymd_fprintf(stdout, "${[!blue]%s}$\n", "Blue");
	ymd_fprintf(stdout, "${[blue]%s}$\n", "Blue");

	ymd_fprintf(stdout, "${[yellow]%d}$:${[green]%f}$:${[blue]%s}$\n",
			1, 0.1, "bound");
	ymd_fprintf(stdout, "%s", "No color!\n");
	return 0;
}
