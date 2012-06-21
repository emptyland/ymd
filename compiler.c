#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DECL_EXPR_PRIV(v) \
	v(LITERAL, 0) \
	v(PAREN,   1) \
	v(INDEX,   2) \
	v(DOT,     3) \
	v(CALL,    4) \
	v(UNQIUE,  5) \
	v(MUL,     6) \
	v(ADD,     7) \
	v(COMP,    8) \
	v(LOGC,    9)

char *strndup(const char *p, size_t n) {
	char *rv = calloc(n + 1, 1);
	strncpy(rv, p, n);
	return rv;
}

static void ymc_fail(struct ymd_parser *p, const char *fmt, ...) {
	va_list ap;
	size_t off;
	char msg[1024];
	snprintf(msg, sizeof(msg), "%s%d:%d ", !p->lex.file ? "%% " : p->lex.file,
	         p->lex.i_line, p->lex.i_column);
	off = strlen(msg);
	va_start(ap, fmt);
	vsnprintf(msg + off, sizeof(msg) - off, fmt, ap);
	va_end(ap);
	puts(msg);
	longjmp(p->jpt, 1);
}

static int ymc_match(struct ymd_parser *p, int need) {
	if (need != p->lah.token)
		ymc_fail(p, "Error token; miss '%c', need '%c'", p->lah.token,
		         need);
	return lex_next(&p->lex, &p->lah);
}

static inline int ymc_peek(struct ymd_parser *p) {
	return p->lah.token;
}

static inline int ymc_next(struct ymd_parser *p) {
	return lex_next(&p->lex, &p->lah);
}

static inline char *ymc_literal(struct ymd_parser *p) {
	return strndup(p->lah.off, p->lah.len);
}

enum ymd_op {
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
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
	default: return OP_NOT_BINARY;
	}
}

static void ymk_unary(struct ymd_parser *p, int op) {
	switch (op) {
	case OP_MINS:
		printf("inv\n");
		break;
	case OP_NOT:
		printf("logc not\n");
		break;
	case OP_NOT_NIL:
		printf("test nn\n");
		break;
	default:
		assert(0);
		break;
	}
}

static void ymk_binary(struct ymd_parser *p, int op) {
	switch (op) {
	case OP_ADD:
		printf("add\n");
		break;
	case OP_SUB:
		printf("sub\n");
		break;
	case OP_MUL:
		printf("mul\n");
		break;
	case OP_DIV:
		printf("div\n");
		break;
	default:
		assert(0);
		break;
	}
}
static void parse_simple(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case NIL:
		printf("push nil\n");
		break;
	case DEC_LITERAL:
		printf("push %s\n", ymc_literal(p));
		break;
	case HEX_LITERAL:
		printf("push %s\n", ymc_literal(p));
		break;
	case SYMBOL:
		printf("push %s\n", ymc_literal(p));
		break;
	default:
		// parse_suffixed(p);
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
		ymc_next(p);
		next_op = parse_expr(p, prio[op].right);
		ymk_binary(p, op);
		op = next_op;
	}
	return op;
}

void ymc_compile(struct ymd_parser *p) {
	lex_next(&p->lex, &p->lah);
	if (setjmp(p->jpt))
		return;
	parse_expr(p, 0);
}
