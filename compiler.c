#include "compiler.h"
#include "memory.h"
#include "state.h"
#include "assembly.h"
#include "encode.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct znode {
	struct znode *next;
	int len;
	char land[1];
};

struct block {
	struct block *chain;
	struct chunk *core;
};

struct loop_info {
	struct loop_info *chain;
	ushort_t i_retry;
	ushort_t i_jcond;
	uint_t jmt[128];  // jumping table
	int njmt;
	int death; // is death loop
};

static inline int ymc_peek(struct ymd_parser *p) {
	return p->lah.token;
}

static inline int ymc_next(struct ymd_parser *p) {
	return lex_next(&p->lex, &p->lah);
}

static inline int ymc_test(struct ymd_parser *p, int token) {
	if (ymc_peek(p) == token) {
		ymc_next(p);
		return 1;
	}
	return 0;
}

static void ymc_fail(struct ymd_parser *p, const char *fmt, ...);
static int parse_expr(struct ymd_parser *p, int limit);
static void parse_stmt(struct ymd_parser *p);
static void parse_return(struct ymd_parser *p);
static void parse_closure(struct ymd_parser *p);
static void parse_block(struct ymd_parser *p);

//------------------------------------------------------------------------------
// Code generating
//------------------------------------------------------------------------------
static inline struct chunk *ymk_chunk() {
	struct chunk *x = vm_zalloc(sizeof(*x));
	blk_add_lz(x, "argv");
	// blk_add_lz(x, "self");
	// ...
	// TODO
	return x;
}

static inline void ymk_enter(struct ymd_parser *p, struct block *blk,
                             struct chunk *core) {
	if (!core) core = ymk_chunk();
	blk->chain = p->blk;
	blk->core = core;
	p->blk = blk;
}

static inline struct chunk *ymk_leave(struct ymd_parser *p) {
	struct chunk *rv = p->blk->core;
	p->blk = p->blk->chain;
	return rv;
}

static inline void ymk_emitOfP(
	struct ymd_parser *self, uchar_t o, uchar_t f, ushort_t p) {
	blk_emit(self->blk->core, asm_build(o, f, p));
}
static inline void ymk_emitOf(
	struct ymd_parser *self, uchar_t o, uchar_t f) {
	ymk_emitOfP(self, o, f, 0);
}
static inline void ymk_emitO(
	struct ymd_parser *self, uchar_t o) {
	ymk_emitOf(self, o, 0);
}
static inline void ymk_emitOP(
	struct ymd_parser *self, uchar_t o, ushort_t p) {
	ymk_emitOfP(self, o, 0, p);
}

static inline ushort_t ymk_ipos(struct ymd_parser *self) {
	const struct chunk *core = self->blk->core;
	return core->kinst;
}

static inline ushort_t ymk_hold(struct ymd_parser *p) {
	ushort_t ipos = ymk_ipos(p);
	ymk_emitO(p, I_PANIC);
	return ipos;
}

static void ymk_emit_int(
	struct ymd_parser *self, ymd_int_t imm) {
	ushort_t partal[MAX_VARINT16_LEN];
	int i, k = varint16_encode(imm, partal) - 1;
	for (i = 0; i < k; ++i)
		ymk_emitOfP(self, I_PUSH, F_PARTAL, partal[i]);
	ymk_emitOfP(self, I_PUSH, F_INT, partal[k]);
}

static void ymk_emit_kz(
	struct ymd_parser *self, const char *raw) {
	char *priv = NULL;
	int i, k = stresc(raw, &priv);
	if (priv) {
		i = blk_kz(self->blk->core, priv, k);
		vm_free(priv);
	} else {
		i = blk_kz(self->blk->core, "", 0);
	}
	ymk_emitOfP(self, I_PUSH, F_ZSTR, i);
}

static void ymk_emit_store(
	struct ymd_parser *self, const char *symbol) {
	int i = blk_find_lz(self->blk->core, symbol);
	if (i < 0) {
		i = blk_kz(self->blk->core, symbol, -1);
		ymk_emitOfP(self, I_STORE, F_OFF, i);
	} else {
		ymk_emitOfP(self, I_STORE, F_LOCAL, i);
	}
}

static void ymk_emit_push(
	struct ymd_parser *self, const char *symbol) {
	int i = blk_find_lz(self->blk->core, symbol);
	if (i < 0) {
		i = blk_kz(self->blk->core, symbol, -1);
		ymk_emitOfP(self, I_PUSH, F_OFF, i);
	} else {
		ymk_emitOfP(self, I_PUSH, F_LOCAL, i);
	}
}

static void ymk_emit_pushz(
	struct ymd_parser *self, const char *symbol, int n) {
	int i = blk_kz(self->blk->core, symbol, n);
	ymk_emitOfP(self, I_PUSH, F_ZSTR, i);
}

static int ymk_new_locvar(
	struct ymd_parser *self, const char *symbol) {
	int i = blk_add_lz(self->blk->core, symbol);
	if (i < 0)
		ymc_fail(self, "Local varibale `%s` duplicate", symbol);
	return i;
}

static void ymk_emit_end(struct ymd_parser *self) {
	struct chunk *core = self->blk->core;
	if (!core->kinst || asm_op(core->inst[core->kinst - 1]) != I_RET)
		ymk_emitOP(self, I_RET, 0);
}

static void ymk_emit_bind(
	struct ymd_parser *self, const char *symbol, struct chunk *core) {
	int i = blk_add_lz(core, symbol);
	if (i < 0)
		ymc_fail(self, "Local varibale `%s` duplicate", symbol);
	ymk_emit_push(self, symbol);
}

static void ymk_emit_setf(
	struct ymd_parser *self, const char *symbol) {
	int i = blk_kz(self->blk->core, symbol, -1);
	ymk_emitOfP(self, I_SETF, F_FAST, i);
}

static void ymk_emit_getf(
	struct ymd_parser *self, const char *symbol) {
	int i = blk_kz(self->blk->core, symbol, -1);
	ymk_emitOfP(self, I_GETF, F_FAST, i);
}

static inline void ymk_emit_jmp(struct ymd_parser *p, uint_t target,
                                int bwd) {
	ushort_t off;
	const uint_t i_curr = p->blk->core->kinst;
	if (bwd) {
		assert(i_curr >= target);
		off = i_curr - target;
	} else {
		assert(i_curr <= target);
		off = target - i_curr;
	}
	ymk_emitOfP(p, I_JMP, bwd ? F_BACKWARD : F_FORWARD, off);
}

static inline void ymk_hack(struct ymd_parser *self, ushort_t i, uint_t inst) {
	const struct chunk *core = self->blk->core;
	assert(i < core->kinst);
	core->inst[i] = inst;
}

static inline void ymk_hack_jmp(
	struct ymd_parser *self, ushort_t i, uchar_t op, int bwd) {
	const struct chunk *core = self->blk->core;
	ushort_t off = core->kinst - i;
	assert(core->kinst >= i);
	ymk_hack(self, i, asm_build(op, bwd ? F_BACKWARD : F_FORWARD, off));
}

static inline char *ymk_literal(struct ymd_parser *p) {
	struct znode *x = vm_zalloc(sizeof(x) + p->lah.len);
	x->len = p->lah.len;
	memcpy(x->land, p->lah.off, x->len);
	return x->land;
}

static const char *ymk_symbol(struct ymd_parser *p) {
	const char *literal;
	if (ymc_peek(p) != SYMBOL)
		ymc_fail(p, "Error token, need symbol.");
	literal = ymk_literal(p);
	ymc_next(p);
	return literal;
}

static inline void ymk_loop_enter(
	struct ymd_parser *p, struct loop_info *info) {
	memset(info, 0, sizeof(*info));
	info->chain = p->loop;
	p->loop = info;
}

static inline void ymk_loop_leave(struct ymd_parser *p) {
	int i;
	const uint_t i_curr = p->blk->core->kinst;
	assert(p->loop != NULL);
	assert(p->loop->i_retry <= i_curr);
	// Jump back
	ymk_emit_jmp(p, p->loop->i_retry, 1);
	// Fillback breack statements
	i = p->loop->njmt;
	while (i--) {
		const uint_t i_jpt = p->loop->jmt[i];
		assert(i_jpt <= i_curr);
		ymk_hack_jmp(p, i_jpt, I_JMP, 0);
	}
	// Fillback foreach
	if (!p->loop->death)
		ymk_hack_jmp(p, p->loop->i_jcond, I_FOREACH, 0);
	p->loop = p->loop->chain;
}

//------------------------------------------------------------------------------
// Compiling
//------------------------------------------------------------------------------
// Token to string
static const struct {
	int len;
	const char *kz;
} kttoa[] = {
#define DEFINE_TOKEN(tok, name) \
	{ sizeof(name) - 1, name, },
DECL_TOKEN(DEFINE_TOKEN)
#undef DEFINE_TOKEN
};
static const char *ymc_ttoa(int token, char buf[2]) {
	size_t i;
	if (token == EOS)
		return "eof";
	if (token == ERROR)
		return "<token error!>";
	if (token < ERROR) {
		buf[0] = (char)token;
		buf[1] = 0;
		return buf;
	}
	i = token - ERROR - 1;
	assert(i < sizeof(kttoa)/sizeof(kttoa[0]));
	return kttoa[i].kz;
}

static void ymc_fail(struct ymd_parser *p, const char *fmt, ...) {
	va_list ap;
	size_t off;
	char msg[1024];
	snprintf(msg, sizeof(msg), "%s:%d:%d ", !p->lex.file ? "%% " : p->lex.file,
	         p->lex.i_line, p->lex.i_column);
	off = strlen(msg);
	va_start(ap, fmt);
	vsnprintf(msg + off, sizeof(msg) - off, fmt, ap);
	va_end(ap);
	puts(msg);
	longjmp(p->jpt, 1);
}

static int ymc_match(struct ymd_parser *p, int need) {
	char buf[2];
	if (need != p->lah.token)
		ymc_fail(p, "Error token! unexpected `%s`, expected `%s`",
		         ymc_ttoa(p->lah.token, buf),
		         ymc_ttoa(need, buf));
	return lex_next(&p->lex, &p->lah);
}

static void parse_array(struct ymd_parser *p) {
	ushort_t count = 0;
	ymc_match(p, '[');
	do {
		switch (ymc_peek(p)) {
		case ']':
			goto out;
		default:
			parse_expr(p, 0);
			++count;
			break;
		}
	} while (ymc_test(p, ','));
out:
	ymk_emitOP(p, I_NEWDYA, count);
}

static void parse_map(struct ymd_parser *p, int ord) {
	ushort_t count = 0;
	ymc_match(p, ord ? SKLS : '{');
	do {
		switch (ymc_peek(p)) {
		case '}':
			goto out;
		case SYMBOL:
			ymk_emit_pushz(p, ymk_symbol(p), -1);
			ymc_match(p, ':');
			parse_expr(p, 0);
			++count;
			break;
		default:
			parse_expr(p, 0);
			ymc_match(p, DICT);
			parse_expr(p, 0);
			++count;
			break;
		}
	} while(ymc_test(p, ','));
out:
	if (ord)
		ymk_emitOP(p, I_NEWSKL, count);
	else
		ymk_emitOP(p, I_NEWMAP, count);
}

static int parse_args(struct ymd_parser *p) {
	int nargs = 1;
	parse_expr(p, 0);
	while (ymc_test(p, ',')) {
		parse_expr(p, 0);
		++nargs;
	}
	return nargs;
}

static void parse_callargs(struct ymd_parser *p, int self) {
	ushort_t nargs = 0;
	(void)self;
	switch (ymc_peek(p)) {
	case '(':
		ymc_next(p);
		if (ymc_peek(p) == ')') {
			ymc_next(p);
			goto out;
		}
		nargs += parse_args(p);
		ymc_match(p, ')');
		break;
	/*case '{':
		parse_map(p, 0);
		nargs += 1;
		ymc_match(p, '}');
		break;*/
	//case STRING:
	//	printf("push %s\n", ymk_symbol(p));
	//	nargs += 1;
	//	break;
	default:
		ymc_fail(p, "Function arguments expected.");
		break;
	}
out:
	ymk_emitOP(p, I_CALL, nargs);
}

static void parse_primary(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '(':
		ymc_next(p);
		parse_expr(p, 0);
		ymc_match(p, ')');
		break;
	case SYMBOL:
		ymk_emit_push(p, ymk_symbol(p));
		break;
	default:
		ymc_fail(p, "Unexpected symbol");
		break;
	}
}

static void parse_suffixed(struct ymd_parser *p) {
	parse_primary(p);
	for (;;) {
		switch (ymc_peek(p)) {
		case '.':
			ymc_next(p);
			ymk_emit_getf(p, ymk_symbol(p));
			break;
		case '[':
			ymc_next(p);
			parse_expr(p, 0);
			ymk_emitOfP(p, I_GETF, F_STACK, 1);
			break;
		case '(': /*case STRING: case '{':*/
			parse_callargs(p, 0);
			break;
		default:
			return;
		}
	}
}

static void parse_simple(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case NIL:
		ymk_emitOf(p, I_PUSH, F_NIL);
		break;
	case TRUE:
		ymk_emitOfP(p, I_PUSH, F_BOOL, 1);
		break;
	case FALSE:
		ymk_emitOfP(p, I_PUSH, F_BOOL, 0);
		break;
	case DEC_LITERAL:
		ymk_emit_int(p, dtoll(ymk_literal(p)));
		break;
	case HEX_LITERAL:
		ymk_emit_int(p, xtoll(ymk_literal(p)));
		break;
	case STRING:
		ymk_emit_kz(p, ymk_literal(p));
		break;
	case '{':
		parse_map(p, 0);
		break;
	case SKLS:
		parse_map(p, 1);
		break;
	case '[':
		parse_array(p);
		break;
	case FUNC:
		parse_closure(p);
		break;
	default:
		parse_suffixed(p);
		return;
	}
	ymc_next(p);
}

static const struct {
	int left;
	int right;
} prio[] = {
	{6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7}, // + - * / %
	{3, 3}, {3, 3}, {3, 3}, // == != ~=
	{3, 3}, {3, 3}, {3, 3}, // < <= > >=
	{2, 2}, {1, 1}, // and or
};
#define PRIO_UNARY 8

enum ymd_op {
	OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
	OP_EQ, OP_NE, OP_MATCH,
	OP_LT, OP_LE, OP_GT, OP_GE,
	OP_AND, OP_OR,
	OP_NOT_BINARY,
	OP_MINS,
	OP_NOT,
	OP_NOT_NIL,
	OP_NOT_UNARY,
};

static int ymc_unary_op(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '-': return OP_MINS;
	case NOT: return OP_NOT;
	case '!': return OP_NOT_NIL;
	default: return OP_NOT_UNARY;
	}
	return 0;
}

static int ymc_binary_op(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '+': return OP_ADD;
	case '-': return OP_SUB;
	case '*': return OP_MUL;
	case '/': return OP_DIV;
	case '%': return OP_MOD;
	case EQ: return OP_EQ;
	case NE: return OP_NE;
	case MATCH: return OP_MATCH;
	case '<': return OP_LT;
	case LE: return OP_LE;
	case '>': return OP_GT;
	case GE: return OP_GE;
	case AND: return OP_AND;
	case OR: return OP_OR;
	default: return OP_NOT_BINARY;
	}
}

static void ymk_unary(struct ymd_parser *p, int op) {
	(void)p;
	switch (op) {
	case OP_MINS:
		ymk_emitO(p, I_INV);
		break;
	case OP_NOT:
	case OP_NOT_NIL:
		ymk_emitO(p, I_NOT);
		break;
	default:
		assert(0);
		break;
	}
}

static ushort_t ymk_hold_logc(struct ymd_parser *p, int op) {
	switch (op) {
	case OP_AND:
	case OP_OR:
		return ymk_hold(p);
	default:
		return 0;
	}
	return 0;
}

static void ymk_binary(struct ymd_parser *p, int op, ushort_t ipos) {
	(void)p;
	switch (op) {
	case OP_ADD:
		ymk_emitO(p, I_ADD);
		break;
	case OP_SUB:
		ymk_emitO(p, I_SUB);
		break;
	case OP_MUL:
		ymk_emitO(p, I_MUL);
		break;
	case OP_DIV:
		ymk_emitO(p, I_DIV);
		break;
	case OP_MOD:
		ymk_emitO(p, I_MOD);
		break;
	case OP_EQ:
		ymk_emitOf(p, I_TEST, F_EQ);
		break;
	case OP_NE:
		ymk_emitOf(p, I_TEST, F_NE);
		break;
	case OP_MATCH:
		ymk_emitOf(p, I_TEST, F_MATCH);
		break;
	case OP_LT:
		ymk_emitOf(p, I_TEST, F_LT);
		break;
	case OP_LE:
		ymk_emitOf(p, I_TEST, F_LE);
		break;
	case OP_GT:
		ymk_emitOf(p, I_TEST, F_GT);
		break;
	case OP_GE:
		ymk_emitOf(p, I_TEST, F_GE);
		break;
	case OP_AND:
		ymk_hack_jmp(p, ipos, I_JNT, 0);
		break;
	case OP_OR:
		ymk_hack_jmp(p, ipos, I_JNN, 0);
		break;
	default:
		assert(0);
		break;
	}
}

static int parse_expr(struct ymd_parser *p, int limit) {
	int op = ymc_unary_op(p);
	if (op != OP_NOT_UNARY) {
		ymc_next(p);
		parse_expr(p, PRIO_UNARY);
		ymk_unary(p, op);
	} else
		parse_simple(p);
	op = ymc_binary_op(p);
	while (op != OP_NOT_BINARY && prio[op].left > limit) {
		int next_op;
		ushort_t i;
		ymc_next(p);
		i = ymk_hold_logc(p, op);
		next_op = parse_expr(p, prio[op].right);
		ymk_binary(p, op, i);
		op = next_op;
	}
	return op;
}

enum {
	VSYMBOL,
	VDOT,
	VINDEX,
	VCALL,
};

struct lval_desc {
	const char *last;
	int vt; // value type
};

static void ymk_lval_partal(struct ymd_parser *p,
                            const struct lval_desc *desc) {
	(void)p;
	switch (desc->vt) {
	case VSYMBOL:
		ymk_emit_push(p, desc->last);
		break;
	case VINDEX:
		ymk_emitOfP(p, I_GETF, F_STACK, 1);
		break;
	case VDOT:
		ymk_emit_setf(p, desc->last);
		break;
	default:
		break;
	}
}

static void ymk_lval_finish(struct ymd_parser *p,
                            const struct lval_desc *desc) {
	(void)p;
	switch (desc->vt) {
	case VSYMBOL:
		ymk_emit_store(p, desc->last);
		break;
	case VINDEX:
		ymk_emitOfP(p, I_SETF, F_STACK, 1);
		break;
	case VDOT:
		ymk_emit_setf(p, desc->last);
		break;
	default:
		ymc_fail(p, "Syntax error, call is not lval");
		break;
	}
}

static void parse_lval(struct ymd_parser *p, struct lval_desc *desc) {
	switch (ymc_peek(p)) {
	case SYMBOL:
		desc->last = ymk_symbol(p);
		desc->vt = VSYMBOL;
		break;
	default:
		ymc_fail(p, "Unexpected symbol");
		break;
	}
	for (;;) {
		switch (ymc_peek(p)) {
		case '.':
			ymk_lval_partal(p, desc);
			ymc_next(p);
			desc->last = ymk_symbol(p);
			desc->vt = VDOT;
			break;
		case '[':
			ymk_lval_partal(p, desc);
			ymc_next(p);
			parse_expr(p, 0);
			ymc_match(p, ']');
			desc->last = NULL;
			desc->vt = VINDEX;
			break;
		case '(': /*case STRING:*/ case '{':
			ymk_lval_partal(p, desc);
			parse_callargs(p, 0);
			desc->last = NULL;
			desc->vt = VCALL;
			break;
		default:
			return;
		}
	}
}

static void parse_expr_stat(struct ymd_parser *p) {
	struct lval_desc desc;
	parse_lval(p, &desc);
	if (ymc_peek(p) == '=') {
		ymc_next(p);
		parse_expr(p, 0);
		ymk_lval_finish(p, &desc);
	} else {
		if (desc.vt != VCALL)
			ymc_fail(p, "Syntax error");
	}
}

static void parse_bind(struct ymd_parser *p, struct chunk *core) {
	ushort_t nbind = 0;
	ymc_next(p);
	do {
		switch (ymc_peek(p)) {
		case ']':
			goto out;
		case SYMBOL:
			ymk_emit_bind(p, ymk_symbol(p), core);
			++nbind;
			break;
		default:
			ymc_fail(p, "Bad bind declare");
			return;
		}
	} while (ymc_test(p, ','));
out:
	ymc_match(p, ']');
	if (nbind == 0)
		ymc_fail(p, "No variable be binded");
	ymk_emitOP(p, I_BIND, nbind);
}

static void parse_params(struct ymd_parser *p) {
	int nparam = 0;
	ymc_match(p, '(');
	do {
		switch (ymc_peek(p)) {
		case ')':
			goto out;
		case SYMBOL:
			ymk_new_locvar(p, ymk_symbol(p));
			++nparam;
			break;
		default:
			ymc_fail(p, "Bad param declare");
			return;
		}
	} while (ymc_test(p, ','));
out:
	ymc_match(p, ')');
	p->blk->core->kargs = nparam;
}

static void parse_block(struct ymd_parser *p) {
	ymc_match(p, '{');
	while (ymc_peek(p) != '}')
		parse_stmt(p);
	ymc_next(p);
}

static void parse_func(struct ymd_parser *p, int local) {
	struct block scope;
	ushort_t i, id;
	struct chunk *blk;
	const char *name;
	ymc_next(p);
	if (ymc_peek(p) != SYMBOL)
		ymc_fail(p, "Function need a name");
	name = ymk_symbol(p);
	i = ymk_hold(p);
	if (local)
		ymk_new_locvar(p, name);
	ymk_emit_store(p, name);
	ymk_enter(p, &scope, ymk_chunk());
		parse_params(p);
		parse_block(p);
		ymk_emit_end(p);
	blk = ymk_leave(p);
	ymd_spawnf(blk, name, &id); // spawn func and fillback code.
	ymk_hack(p, i, emitAP(LOAD, id));
}

static void parse_closure(struct ymd_parser *p) {
	struct block scope;
	ushort_t i, id;
	struct chunk *blk = ymk_chunk();
	char name[128];
	ymc_next(p);
	i = ymk_hold(p);
	if (ymc_peek(p) == '[')
		parse_bind(p, blk);
	ymk_enter(p, &scope, blk);
		parse_params(p);
		parse_block(p);
		ymk_emit_end(p);
	blk = ymk_leave(p);
	snprintf(name, sizeof(name), "__blk_xl%d__",
	         p->lex.i_line);
	ymd_spawnf(blk, name, &id); // spawn func and fillback code.
	ymk_hack(p, i, emitAP(LOAD, id));
}

static void parse_return(struct ymd_parser *p) {
	ymc_next(p); // return
	parse_expr(p, 0);
	ymk_emitOP(p, I_RET, 1);
}

static void parse_decl(struct ymd_parser *p) {
	int i = ymk_new_locvar(p, ymk_symbol(p));
	if (ymc_peek(p) != '=')
		return;
	ymc_next(p);
	parse_expr(p, 0);
	ymk_emitOfP(p, I_STORE, F_LOCAL, i);
}

static void parse_local(struct ymd_parser *p) {
	ymc_next(p);
	if (ymc_peek(p) == FUNC) {
		parse_func(p, 1);
		return;
	}
	parse_decl(p);
	while (ymc_test(p, ',')) parse_decl(p);
}

static void parse_if(struct ymd_parser *p) {
	ushort_t jnxt, jout[128];
	int i = 0, nsub = 0;
	ymc_next(p); // `if` `expr` { `block` }
	parse_expr(p, 0);
	jnxt = ymk_hold(p);
	parse_block(p);
	while (ymc_peek(p) == ELIF) { // `elif` `expr` { `block` }
		ymc_next(p);
		jout[i++] = ymk_hold(p);
		ymk_hack_jmp(p, jnxt, I_JNE, 0);
		parse_expr(p, 0);
		jnxt = ymk_hold(p);
		parse_block(p);
		++nsub;
	}
	if (ymc_peek(p) == ELSE) { // `else` { `block` }
		ymc_next(p);
		jout[i++] = ymk_hold(p);
		ymk_hack_jmp(p, jnxt, I_JNE, 0);
		parse_block(p);
		++nsub;
	}
	while (i--) ymk_hack_jmp(p, jout[i], I_JMP, 0);
	if (nsub == 0) ymk_hack_jmp(p, jnxt, I_JNE, 0);
}

static void parse_for(struct ymd_parser *p) {
	const char *tmp;
	char iter[64];
	struct loop_info scope;
	ymc_next(p); // `for` `symbol` : `call`
	ymk_loop_enter(p, &scope);
	switch (ymc_peek(p)) {
	case '{':
		scope.death = 1;
		p->loop->i_retry = ymk_ipos(p); // set retry
		goto done;
	case VAR:
		ymc_next(p);
		tmp = ymk_symbol(p);
		ymk_new_locvar(p, tmp);
		break;
	case SYMBOL:
		tmp = ymk_symbol(p);
		break;
	default:
		ymc_fail(p, "Syntax error");
		return;
	}
	ymc_match(p, ':'); // : expr
	parse_expr(p, 0);
	snprintf(iter, sizeof(iter), "__loop_iter_%d__", p->for_id++);
	ymk_emitOfP(p, I_STORE, F_LOCAL, ymk_new_locvar(p, iter));

	p->loop->i_retry = ymk_ipos(p); // set retry
	ymk_emit_push(p, iter); // push iter
	ymk_emitOP(p, I_CALL, 0); // call 0
	p->loop->i_jcond = ymk_hold(p); // set jcond foreach
	ymk_emit_store(p, tmp); // store tmp
done:
	parse_block(p); // { `block` }
	ymk_loop_leave(p);
}

static void parse_goto(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case BREAK:
		p->loop->jmt[p->loop->njmt++] = ymk_hold(p);
		break;
	case CONTINUE:
		ymk_emit_jmp(p, p->loop->i_retry, 1);
		break;
	}
	ymc_next(p);
}

static void parse_stmt(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case ';':
		ymc_next(p); // Empty statement
		break;
	case IF:
		parse_if(p);
		break;
	case WITH:
		//parse_with(p);
		break;
	case FOR:
		parse_for(p);
		break;
	case FUNC:
		parse_func(p, 0);
		break;
	case VAR:
		parse_local(p);
		break;
	case RETURN:
		parse_return(p);
		break;
	case BREAK:
	case CONTINUE:
		parse_goto(p);
		break;
	default:
		parse_expr_stat(p);
		break;
	}
}

static void ymc_final(struct ymd_parser *p) {
	{
		struct znode *i = p->lnk, *last = i;
		while (i) {
			last = i;
			i = i->next;
			vm_free(last);
		}
		p->lnk = NULL;
	}
	{
		struct block *i = p->blk, *last = i;
		while (i) {
			last = i;
			i = i->chain;
			if (!last->core->ref) vm_free(last->core);
		}
		p->blk = NULL;
	}
}

struct chunk *ymc_compile(struct ymd_parser *p) {
	struct block scope;
	lex_next(&p->lex, &p->lah);
	ymk_enter(p, &scope, NULL);
	if (setjmp(p->jpt)) {
		ymc_final(p);
		return NULL;
	}
	while (ymc_peek(p) != EOS)
		parse_stmt(p);
	ymk_emit_end(p);
	ymk_leave(p);
	assert(!p->blk);
	ymc_final(p);
	blk_shrink(scope.core);
	return scope.core;
}

struct func *func_compile(const char *name,
                          const char *fnam,
                          const char *code) {
	struct chunk *blk;
	struct ymd_parser p;
	memset(&p, 0, sizeof(p));
	lex_init(&p.lex, fnam, code);
	blk = ymc_compile(&p);
	if (!blk)
		return NULL;
	return func_new(blk, name);
}

struct func *func_compilef(const char *name,
                           const char *fnam,
                           FILE *fp) {
	long len;
	int rv;
	struct func *fn = NULL;
	char *code = NULL;
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	rewind(fp);
	code = vm_zalloc(len + 1);
	rv = fread(code, 1, len, fp);
	if (rv <= 0)
		goto fail;
	fn = func_compile(name, fnam, code);
fail:
	if (code) vm_free(code);
	return fn;
}
