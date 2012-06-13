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
#define MAX_RCD   128

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

struct loop_info {
	struct loop_info *chain;
	ushort_t enter;
	ushort_t leave;
	uint_t rcd[MAX_RCD];
	int nrcd;
};

struct cond_info {
	struct cond_info *chain;
	ushort_t enter;
	ushort_t leave;
	uint_t branch[MAX_RCD];
	int nbranch;
};

struct scope_entry {
	struct func *fn;
	struct loop_info *loop;
	struct cond_info *cond;
};

struct scope_info {
	struct scope_entry nested[MAX_SCOPE];
	int n;
	char err[MAX_ERR];
	int rv;
};

struct info_keeped {
	struct symbol *sym;
	struct scope_info sok;
};
static struct info_keeped infk;

static inline struct scope_entry *sop_curr() {
	return infk.sok.nested + infk.sok.n - 1;
}

#define sym    (infk.sym)
#define loop_i (sop_curr()->loop)
#define cond_i (sop_curr()->cond)
#define sok    (infk.sok)

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
	sok.nested[sok.n++].fn = fn;
	return sok.n;
}

void sop_final(struct scope_entry *x) {
	x->fn = NULL;
	while (x->loop) {
		struct loop_info *i = x->loop;
		x->loop = x->loop->chain;
		free(i);
	}
	while (x->cond) {
		struct cond_info *i = x->cond;
		x->cond = x->cond->chain;
		free(i);
	}
}

struct func *sop_pop() {
	struct func *x;
	assert(sok.n > 0);
	x = sok.nested[sok.n - 1].fn;
	sop_final(&sok.nested[--sok.n]);
	return x;
}

int sop_count() { return sok.n; }
int sop_result() { return sok.rv; }

struct func *sop_index(int i) {
	if (i > 0)
		assert(i < sok.n);
	else
		assert(i >= -sok.n);
	return i < 0 ? sok.nested[sok.n + i].fn : sok.nested[i].fn;
}

void sop_error(const char *err, int rv) {
	strncpy(sok.err, err, sizeof(sok.err));
	sok.rv = rv;
	printf("%s", err);
}

void sop_adjust(struct func *fn) {
	int i = fn->u.core->kinst;
	if (i < 2)
		return;
	i >>= 1;
	while (i--) {
		uint_t tmp = fn->u.core->inst[i];
		uchar_t op = asm_op(fn->u.core->inst[i]);
		assert(op == I_STORE);
		fn->u.core->inst[i] = fn->u.core->inst[fn->u.core->kinst - i - 1];
		fn->u.core->inst[fn->u.core->kinst - i - 1] = tmp;
	}
}

static uint_t hack_fill(uint_t inst, int dict, ushort_t off) {
	uchar_t op = asm_op(inst);
	assert(op == I_JNE || op == I_JMP || op == I_FOREACH);
	assert(asm_flag(inst) == F_UNDEF);
	return asm_build(op, dict < 0 ? F_BACKWARD : F_FORWARD, off);
}

// Compiling information functions:
void info_loop_push(ushort_t pos) {
	struct loop_info *x = calloc(sizeof(*x), 1);
	x->chain = loop_i;
	x->enter = pos;
	loop_i = x;
}

ushort_t info_loop_off(const struct func *fn) {
	assert(fn->u.core->kinst >= loop_i->enter);
	return fn->u.core->kinst - loop_i->enter;
}

void info_loop_back(struct func *fn, int death) {
	int i;
	uint_t k, old;
	// Fill enter
	loop_i->leave = fn->u.core->kinst;
	if (!death) {
		old = fn->u.core->inst[loop_i->enter];
		assert(loop_i->leave >= loop_i->enter);
		fn->u.core->inst[loop_i->enter] = hack_fill(old, 1, loop_i->leave - loop_i->enter);
	}
	// Fill break statment or continue statment
	i = loop_i->nrcd;
	while (i--) {
		k = loop_i->rcd[i] & 0xffff;
		old = fn->u.core->inst[k];
		if (loop_i->rcd[i] & 0x80000000) { // break
			assert(loop_i->leave >= k);
			fn->u.core->inst[k] = hack_fill(old,  1, loop_i->leave - k);
		} else {
			assert(loop_i->enter <= k);
			fn->u.core->inst[k] = hack_fill(old, -1, k - loop_i->enter);
		}
	}
}

void info_loop_pop() {
	struct loop_info *x;
	assert(loop_i != NULL);
	x = loop_i->chain;
	free(loop_i);
	loop_i = x;
}

void info_loop_rcd(char flag, ushort_t pos) {
	assert(loop_i != NULL);
	if (loop_i->nrcd >= MAX_RCD) {
		vm_die("So many break/continue statments");
		return;
	}
	switch (flag) {
	case 'b':
		loop_i->rcd[loop_i->nrcd++] = (0x80000000 | pos);
		break;
	case 'c':
		loop_i->rcd[loop_i->nrcd++] = pos;
		break;
	default:
		assert(0);
		break;
	}
}

void info_cond_push(ushort_t pos) {
	struct cond_info *x = calloc(sizeof(*x), 1);
	x->chain = cond_i;
	x->enter = pos;
	cond_i = x;
}

void info_cond_back(struct func *fn) {
	ushort_t bak[MAX_RCD];
	int i, n = 0;
	uint_t k, p, old;

	cond_i->leave = fn->u.core->kinst;
	bak[n++] = cond_i->enter;
	// 8: out
	// 4: branch
	// 2: last
	for (i = 0; i < cond_i->nbranch; ++i) {
		if (cond_i->branch[i] & 0x80000000) {
			k = cond_i->branch[i] & 0xffff;
			old = fn->u.core->inst[k];
			assert(cond_i->leave >= k);
			fn->u.core->inst[k] = hack_fill(old, 1, cond_i->leave - k);
		} else
			bak[n++] = cond_i->branch[i] & 0xffff;
	}
	// Last label is not last `else` stmt
	if (n % 2 == 1)
		bak[n++] = cond_i->leave;
	for (i = 0; i < n; i += 2) {
		k = bak[i];
		p = bak[i + 1];
		old = fn->u.core->inst[k];
		assert(p >= k);
		fn->u.core->inst[k] = hack_fill(old, 1, p - k);
	}
}

void info_cond_pop() {
	struct cond_info *x;
	assert(cond_i != NULL);
	x = cond_i->chain;
	free(cond_i);
	cond_i = x;
}

// 8: out
// 4: branch
// 2: last
void info_cond_rcd(char which, ushort_t pos) {
	assert(cond_i != NULL);
	if (cond_i->nbranch >= MAX_RCD) {
		vm_die("So many if/else/elif statments");
		return;
	}
	switch (which) {
	case 'o': // [o]ut
		cond_i->branch[cond_i->nbranch++] = (0x80000000 | pos);
		break;
	case 'b': // [b]ranch
		cond_i->branch[cond_i->nbranch++] = (0x40000000 | pos);
		break;
	default:
		assert(0);
		break;
	}
}
