#include "symbol.h"
#include "value.h"
#include "state.h"
#include "assembly.h"
#include "memory.h"
#include <string.h>
#include <stdio.h>

#define MAX_SLOT       16
#define MAX_STACK      128
#define MAX_SCOPE      128
#define MAX_ERR        260
#define MAX_SYMBOL_LEN 260
#define MAX_ITER_LEN   64
#define MAX_RCD        128

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
	int enter;
	int leave;
	int retry;
	int jcond;
	uint_t rcd[MAX_RCD];
	int nrcd;
	int seq;
	char id[MAX_SYMBOL_LEN];
	char iter[MAX_ITER_LEN];
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


void emit_access(unsigned char inst, const char *z) {
	int i = func_find_lz(sop(), z); 
	if (i < 0) {
		int k = func_kz(sop(), z, -1);
		func_emit(sop(), asm_build(inst, F_OFF, k));
	} else {
		func_emit(sop(), asm_build(inst, F_LOCAL, i));
	}
}

void emit_bind(const char *z) {
	struct func *up = sop_index(-2);
	int i = func_find_lz(up, z);
	if (i < 0) {
		int k = func_kz(up, z, -1);
		func_emit(up, emitAfP(PUSH, OFF, k));
	} else {
		func_emit(up, emitAfP(PUSH, LOCAL, i));
	}
	++sop()->n_bind;
	func_add_lz(sop(), z); // FIXME
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
void info_loop_push(int pos) {
	struct loop_info *x = calloc(sizeof(*x), 1);
	x->chain = loop_i;
	x->enter = pos;
	x->retry = pos;
	if (loop_i)
		x->seq = loop_i->seq + 1;
	else
		x->seq = 0;
	snprintf(x->iter, sizeof(x->iter), "__loop_seq_%d_xx__", x->seq);
	loop_i = x;
}

ushort_t info_loop_off(const struct func *fn) {
	assert(fn->u.core->kinst >= loop_i->enter);
	return (ushort_t)(fn->u.core->kinst - loop_i->retry);
}

void info_loop_back(struct func *fn, int death) {
	int i;
	uint_t k, old;
	struct chunk *core = fn->u.core;
	// Fill enter
	loop_i->leave = core->kinst;
	if (!death) {
		old = core->inst[loop_i->jcond];
		assert(loop_i->leave >= loop_i->jcond);
		core->inst[loop_i->jcond] = hack_fill(old, 1,
			loop_i->leave - loop_i->jcond);
	}
	// Fill break statment or continue statment
	i = loop_i->nrcd;
	while (i--) {
		k = loop_i->rcd[i] & 0xffff;
		old = core->inst[k];
		if (loop_i->rcd[i] & 0x80000000) { // break
			assert((uint_t)loop_i->leave >= k);
			core->inst[k] = hack_fill(old,  1, loop_i->leave - k);
		} else {
			assert((uint_t)loop_i->enter <= k);
			core->inst[k] = hack_fill(old, -1, k - loop_i->retry);
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

void info_loop_rcd(char flag, int pos) {
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

void info_loop_set_retry(int pos) {
	loop_i->retry = pos;
}

void info_loop_set_jcond(int pos) {
	loop_i->jcond =pos;
}

const char *info_loop_iter_name() {
	return loop_i->iter;
}

void info_loop_set_id(const char *id) {
	strncpy(loop_i->id, id, sizeof(loop_i->id));
}

const char *info_loop_id() {
	return loop_i->id;
}

void info_cond_push(int pos) {
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
void info_cond_rcd(char which, int pos) {
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

static inline long long xtol(char x) {
	if (x >= '0' && x <= '9')
		return x - '0';
	else if (x >= 'a' && x <= 'f')
		return 16 + x - 'a';
	else if (x >= 'A' && x <= 'F')
		return 16 + x - 'A';
	assert(0);
	return -1;
}

static inline long long dtol(char d) {
	if (x >= '0' && x <= '9')
		return x - '0';
	assert(0);
	return -1;
}

#define ESC_CHAR(n, c) \
	case n: \
		priv[n++] = c; \
		break
int stresc(const char *i, char **rv) {
	char *priv = NULL;
	int n = 0;
	while (*i) {
		priv = mm_need(priv, n, 64, 1);
		if (*i++ == '\\') {
			switch (*i++) {
			ESC_CHAR('a', '\a');
			ESC_CHAR('b', '\b');
			ESC_CHAR('f', '\f');
			ESC_CHAR('n', '\n');
			ESC_CHAR('r', '\r');
			ESC_CHAR('t', '\t');
			ESC_CHAR('v', '\v');
			ESC_CHAR('\\', '\\');
			ESC_CHAR('\"', '\"');
			ESC_CHAR('\0', '\0');
			default:
				vm_free(priv);
				return -1;
			}
			continue;
		}
		priv[n] = *i;
		++i; ++n;
	}
}
#undef ESC_CHAR

long long xtoll(const char *raw) {
	long long rv = 0;
	int k = strlen(raw), i = k;
	while (i--)
		rv |= (xtol(raw[i]) << ((k - i - 1) * 16));
	return rv;
}

long long dtoll(const char *raw) {
	long long rv = 0;
	long long pow = 1;
	int k = strlen(raw), i = k;
	while (i--) {
		rv += (dtol(raw[i]) * pow);
		pow *= 10;
	}
	return rv;
}
