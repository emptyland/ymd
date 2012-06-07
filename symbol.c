#include "symbol.h"
#include "value.h"
#include "state.h"
#include "assembly.h"
#include "memory.h"
#include <string.h>
#include <stdio.h>

#define MAX_SLOT  16
#define MAX_STACK 128
#define MAX_SCOPE 128
#define MAX_ERR   260

struct slot {
	struct slot *prev;
	int elem[MAX_SLOT];
};
struct symbol {
	struct symbol *prev;
	struct slot *cnt;
	char *name[MAX_STACK];
	int n;
	int last_elem_s[MAX_SLOT];
	int last_elem[MAX_SLOT];
};
struct scopes {
	struct func *nested[MAX_SCOPE];
	int n;
	char err[MAX_ERR];
	int rv;
};
struct info_keeped {
	struct symbol *sym;
	struct loop_info *loop;
	struct cond_info *cond;
	struct scopes sok;
};
static struct info_keeped infk;

#define sym  (infk.sym)
#define loop (infk.loop)
#define cond (infk.cond)
#define sok  (infk.sok)

int sym_push(const char *z, int n) {
	if (n >= 0) {
		sym->name[sym->n] = malloc(n + 1);
		strncpy(sym->name[sym->n], z, n);
		sym->name[sym->n++][n] = 0;
	}
	else
		sym->name[sym->n++] = strdup(z);
	return sym->n - 1;
}

int sym_count() {
	return sym->n;
}

const char *sym_index(int i) {
	return i < 0 ? sym->name[sym->n + i] : sym->name[i];
}

void sym_pop() {
	int i = sym_count();
	while (i--) {
		free(sym->name[i]);
		sym->name[i] = NULL;
	}
	sym->n = 0;
}

void sym_scope_begin() {
	struct symbol *top = calloc(sizeof(struct symbol), 1);
	top->prev = sym;
	sym = top;
}

void sym_scope_end() {
	if (sym->prev)
		memcpy(sym->prev->last_elem_s,
		       sym->last_elem,
			   sizeof(sym->last_elem));
	sym_pop();
	struct symbol *tmp = sym;
	sym = sym->prev;
	free(tmp);
}

void sym_slot_begin() {
	struct slot *top = calloc(sizeof(struct slot), 1);
	top->prev = sym->cnt;
	sym->cnt = top;
}

void sym_slot_end() {
	memcpy(sym->last_elem, sym->cnt->elem,
	       sizeof(sym->last_elem));
	struct slot *tmp = sym->cnt;
	sym->cnt = sym->cnt->prev;
	free(tmp);
}

int sym_slot(int i, int op) {
	sym->cnt->elem[i] += op;
	return sym->cnt->elem[i];
}

int sym_last_slot(int i, int lv) {
	switch (lv) {
	case 0:
		return sym->last_elem[i];
	case 1:
		return sym->last_elem_s[i];
	}
	return 0;
}

int sop_push(struct func *fn) {
	if (sok.n >= MAX_SCOPE) {
		vm_die("So many function nested");
		return -1;
	}
	sok.nested[sok.n++] = fn;
	return sok.n;
}

struct func *sop_pop() {
	struct func *x;
	assert(sok.n > 0);
	x = sok.nested[sok.n - 1];
	sok.nested[--sok.n] = NULL;
	return x;
}

int sop_count() { return sok.n; }
int sop_result() { return sok.rv; }

struct func *sop_index(int i) {
	if (i > 0)
		assert(i < sok.n);
	else
		assert(i >= -sok.n);
	return i < 0 ? sok.nested[sok.n + i] : sok.nested[i];
}

void sop_error(const char *err, int rv) {
	strncpy(sok.err, err, sizeof(sok.err));
	sok.rv = rv;
	printf("%s", err);
}

// Compiling information functions:
void info_loop_push(ushort_t pos) {
	struct loop_info *x = calloc(sizeof(*x), 1);
	x->chain = loop;
	x->enter = pos;
	loop = x;
}

ushort_t info_loop_off(const struct func *fn) {
	assert(fn->n_inst >= loop->enter);
	return fn->n_inst - loop->enter;
}

void info_loop_back(const struct func *fn);
void info_loop_pop();
void info_loop_rcd(char flag, ushort_t pos) {
	assert(loop != NULL);
	assert(loop->nrcd <= MAX_NRCD);
	switch (flag) {
	case 'b':
		loop->rcd[loop->nrcd++] = (0x80000000 | pos);
		break;
	case 'c':
		loop->rcd[loop->nrcd++] = pos;
		break;
	default:
		assert(0);
		break;
	}
}

