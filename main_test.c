#include "state.h"
#include "yut.h"

int main(int argc, char *argv[]) {
	int rv;
	vm_init();
	vm_init_context();
	rv = yut_run_all(argc, argv);
	vm_final_context();
	vm_final();
	return rv;
}
