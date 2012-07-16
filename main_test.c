#include "state.h"
#include "yut.h"

struct ymd_mach *tvm;

int main(int argc, char *argv[]) {
	int rv;
	tvm = ymd_init();
	ymd_init_context(tvm);
	rv = yut_run_all(argc, argv);
	ymd_final_context(tvm);
	ymd_final(tvm);
	return rv;
}
