#include <stdio.h>

int foo(int i) {
	return i + 1;
}

static const int g_i = foo(100);


int main(int argc, char *argv[]) {
	printf("Hello, Fucker! %d\n", g_i);
}
