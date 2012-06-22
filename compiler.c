#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

char *strndup(const char *p, size_t n) {
	char *rv = calloc(n + 1, 1);
	strncpy(rv, p, n);
	return rv;
}

static int parse_expr(struct ymd_parser *p, int limit);

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

static int ymc_test(struct ymd_parser *p, int token) {
	if (ymc_peek(p) == token) {
		ymc_next(p);
		return 1;
	}
	return 0;
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
	(void)p;
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
	(void)p;
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

static const char *ymc_symbol(struct ymd_parser *p) {
	const char *literal;
	if (ymc_peek(p) != SYMBOL)
		ymc_fail(p, "Error token, need symbol.");
	literal = ymc_literal(p);
	ymc_next(p);
	return literal;
}

static void parse_array(struct ymd_parser *p) {
	int count = 0;
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
	printf("newdyay %d\n", count);
}

static void parse_map(struct ymd_parser *p, int ord) {
	int count = 0;
	ymc_match(p, ord ? SKLS : '{');
	do {
		switch (ymc_peek(p)) {
		case '}':
			goto out;
		case SYMBOL:
			printf("push %s\n", ymc_symbol(p));
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
		printf("newskls %d\n", count);
	else
		printf("newhmap %d\n", count);
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
	int nargs = self, type = 0;
	switch (ymc_peek(p)) {
	case '(':
		type = 0;
		ymc_next(p);
		if (ymc_peek(p) == ')') {
			ymc_next(p);
			goto out;
		}
		nargs += parse_args(p);
		ymc_match(p, ')');
		break;
	case '{':
		type = 1;
		parse_map(p, 0);
		nargs += 1;
		ymc_match(p, '}');
		break;
	//case STRING:
	//	printf("push %s\n", ymc_symbol(p));
	//	nargs += 1;
	//	break;
	default:
		ymc_fail(p, "Function arguments expected.");
		break;
	}
out:
	printf("call %d\n", nargs);
}

static void parse_primary(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '(':
		ymc_next(p);
		parse_expr(p, 0);
		ymc_match(p, ')');
		break;
	case SYMBOL:
		printf("push %s\n", ymc_symbol(p));
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
			printf("getf %s\n", ymc_symbol(p));
			break;
		case '[':
			ymc_next(p);
			parse_expr(p, 0);
			printf("getf 1\n");
			break;
		case '(': /*case STRING:*/ case '{':
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
		printf("push nil\n");
		break;
	case TRUE:
		printf("push true\n");
		break;
	case FALSE:
		printf("push false\n");
		break;
	case DEC_LITERAL:
		printf("push %s\n", ymc_literal(p));
		break;
	case HEX_LITERAL:
		printf("push %s\n", ymc_literal(p));
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
		printf("push %s\n", desc->last);
		break;
	case VINDEX:
		printf("index\n");
		break;
	case VDOT:
		printf("getf %s\n", desc->last);
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
		printf("store %s\n", desc->last);
		break;
	case VINDEX:
		printf("setf 1\n");
		break;
	case VDOT:
		printf("setf %s\n", desc->last);
		break;
	default:
		ymc_fail(p, "Syntax error, call is not lval");
		break;
	}
}

static void parse_lval(struct ymd_parser *p, struct lval_desc *desc) {
	switch (ymc_peek(p)) {
	case SYMBOL:
		desc->last = ymc_symbol(p);
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
			desc->last = ymc_symbol(p);
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

static void parse_stmt(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case ';':
		ymc_next(p); // Empty statement
		break;
	case IF:
		//parse_if(p);
		break;
	case WITH:
		//parse_with(p);
		break;
	case FOR:
		//parse_for(p);
		break;
	case FUNC:
		//parse_func(p);
		break;
	case VAR:
		//parse_local_decl(p);
		break;
	case RETURN:
		//parse_return(p);
		break;
	case BREAK:
	case CONTINUE:
		//parse_goto(p);
		break;
	default:
		parse_expr_stat(p);
		break;
	}
}

void ymc_compile(struct ymd_parser *p) {
	lex_next(&p->lex, &p->lah);
	if (setjmp(p->jpt))
		return;
	parse_stmt(p);
}
