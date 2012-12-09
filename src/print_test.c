#include "print.h"
#include "yut_rand.h"
#include "yut.h"

static int test_print_display() {
	ymd_printf("<0>"yRED"<red>"yEND"\n");
	ymd_printf("<1>"yGREEN"<green>"yEND"\n");
	ymd_printf("<2>"yYELLOW"<yellow>"yEND"\n");
	ymd_printf("<3>"yBLUE"<blue>"yEND"\n");
	ymd_printf("<4>"yPURPLE"<purple>"yEND"\n");
	ymd_printf("<5>"yAZURE"<azure>"yEND"\n");
	ymd_printf("<6>"yDRED"<deep red>"yEND"\n");
	ymd_printf("<7>"yDGREEN"<deep green>"yEND"\n");
	ymd_printf("<8>"yDYELLOW"<deep yellow>"yEND"\n");
	ymd_printf("<9>"yDBLUE"<deep blue>"yEND"\n");
	ymd_printf("<A>"yDPURPLE"<deep purple>"yEND"\n");
	ymd_printf("<B>"yDAZURE"<deep azure>"yEND"\n");
	ymd_set_colored(0);
	ymd_printf("<0>"yRED"<red>"yEND"\n");
	ymd_printf("<1>"yGREEN"<green>"yEND"\n");
	ymd_printf("<2>"yYELLOW"<yellow>"yEND"\n");
	ymd_printf("<3>"yBLUE"<blue>"yEND"\n");
	ymd_printf("<4>"yPURPLE"<purple>"yEND"\n");
	ymd_printf("<5>"yAZURE"<azure>"yEND"\n");
	ymd_printf("<6>"yDRED"<deep red>"yEND"\n");
	ymd_printf("<7>"yDGREEN"<deep green>"yEND"\n");
	ymd_printf("<8>"yDYELLOW"<deep yellow>"yEND"\n");
	ymd_printf("<9>"yDBLUE"<deep blue>"yEND"\n");
	ymd_printf("<A>"yDPURPLE"<deep purple>"yEND"\n");
	ymd_printf("<B>"yDAZURE"<deep azure>"yEND"\n");
	ymd_set_colored(1);
	return 0;
}

static int test_print_format() {
	ymd_printf(yAZURE"<0>"yEND yRED"[%u]:"yEND yPURPLE"[%s]:"yEND"%s\n",
			   0xffffffff, "Hello, World!", "Suck is huge!");
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(print_display, normal)
	TEST_ENTRY(print_format, normal)
TEST_END
