#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECL_EXPR_PRIV(v) \
	v(LITERAL, 0) \
	v(PAREN,   1) \
	v(INDEX,   2) \
	v(DOT,     3) \
	v(CALL,    4) \
	v(UNQIUE,  5) \
	v(MUL,     6) \
	v(ADD,     7)

#if 0
static inline char *strndup(const char *p, size_t n) {
	char *rv = calloc(n + 1, 1);
	strncpy(rv, p, n);
	return rv;
}
#endif

static void parse_expr(struct ymd_parser *p);
static void parse_expr_priv(struct ymd_parser *p, int cpl, int priv);
static int parse_args(struct ymd_parser *p);
static int parse_array(struct ymd_parser *p);
static int parse_map(struct ymd_parser *p, int ord);
static void parse_rval(struct ymd_parser *p);
//static void parse_stmt(struct ymd_parser *p);
//static void parse_assign(struct ymd_parser *p);

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

static inline char *ymc_literal(struct ymd_parser *p) {
	return strndup(p->lah.off, p->lah.len);
}

static inline int ymc_term(int token) {
	switch (token) {
	case EL: case ERROR: case EOS: case ')': case ']': case '}':
	case ',': case SKLS:
		return 1;
	}
	return 0;
}

/*
static void parse_stmt(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case VAR:
		// TODO:
		break;
	case FUNC:
		// TODO:
		break;
	case SYMBOL:
		parse_assign(p);
		break;
	}
}*/

// assign ::= lval = rval
//
// lval ::= expr . symbol
//        | expr [ expr ]
//        | symbol
/*
static void parse_assign(struct ymd_parser *p) {
	const char *last = ymc_literal(p);
	ymc_match(p, SYMBOL);
	switch (ymc_peek(p)) {
	case '=':
		parse_rval(p);
		printf("store %s\n", last);
		break;
	case '.':
		ymc_match(p, '.');
		parse_accl(p);
		printf("setf 1\n");
		break;
	case '[':
		parse_accl(p);
		printf("setf 1\n");
		break;
	}
}

static void parse_accl(struct ymd_parser *p) {
	int token = ymc_peek(p);
	if (token == SYMBOL) {
		const char *last = ymc_literal(p);
		ymc_match(p, SYMBOL);
		if (ymc_peek(p) == '=')

	}
}*/

// args ::= rval , args
//        | rval
//        |
static int parse_args(struct ymd_parser *p) {
	int argc = 0;
	for (;;) {
		switch (ymc_peek(p)) {
		case ',':
			if (argc > 0)
				ymc_match(p, ',');
			break;
		case ')':
			goto done;
		default:
			parse_rval(p); ++argc;
			break;
		}
	}
done:
	return argc;
}

static void parse_rval(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '[':
		parse_array(p);
		break;
	case '{':
		parse_map(p, 0);
		break;
	case SKLS:
		parse_map(p, 1);
		break;
	default:
		parse_expr(p);
		break;
	}
}

static int parse_map(struct ymd_parser *p, int ord) {
	int count = 0;
	if (ord)
		ymc_match(p, SKLS);
	else
		ymc_match(p, '{');
	for (;;) {
		switch (ymc_peek(p)) {
		case ',':
			if (count > 0)
				ymc_match(p, ',');
			break;
		case '}':
			goto done;
		case SYMBOL:
			printf("push %s\n", ymc_literal(p));
			ymc_match(p, SYMBOL);
			ymc_match(p, ':');
			parse_rval(p);
			++count;
			break;
		default:
			parse_rval(p);
			ymc_match(p, DICT);
			parse_rval(p);
			++count;
			break;
		}
	}
done:
	ymc_match(p, '}');
	if (ord)
		printf("newskls %d\n", count);
	else
		printf("newhmap %d\n", count);
	return count;
}

static int parse_array(struct ymd_parser *p) {
	int count = 0;
	ymc_match(p, '[');
	for (;;) {
		switch (ymc_peek(p)) {
		case ',':
			if (count > 0)
				ymc_match(p, ',');
			break;
		case ']':
			goto done;
		default:
			parse_rval(p); ++count;
			break;
		}
	}
done:
	ymc_match(p, ']');
	printf("newdyay %d\n", count);
	return count;
}

#define DEFINE_EXPR_PRIV(name, priv) \
	PRIV_##name = priv,
enum expr_priv {
DECL_EXPR_PRIV(DEFINE_EXPR_PRIV)
};
#undef DEFINE_EXPR_PRIV

#define DEFINE_EXPR_SWITCH(name, priv) \
	case PRIV_##name: goto x##name; break;
static void parse_expr_priv(struct ymd_parser *p, int cpl, int priv) {
	int token;
	switch (priv) {
		DECL_EXPR_PRIV(DEFINE_EXPR_SWITCH)
	default:
		goto done;
	}
xLITERAL:
	token = ymc_peek(p);
	switch (token) {
	case DEC_LITERAL: 
		printf("push %s\n", ymc_literal(p));
		ymc_match(p, token);
		goto done;
	case HEX_LITERAL:
		printf("push %s\n", ymc_literal(p));
		ymc_match(p, token);
		goto done;
	case SYMBOL:
		printf("push %s\n", ymc_literal(p));
		ymc_match(p, token);
		goto accl;
	default:
		break;
	}
xPAREN: 
	if (ymc_peek(p) == '(') {
		ymc_match(p, '(');
		parse_expr_priv(p, 1, priv - 1);
		ymc_match(p, ')');
		goto accl;
	}
	goto done;
xINDEX:
	if (cpl) parse_expr(p);
	if (ymc_peek(p) == '[') {
		ymc_match(p, '[');
		parse_expr_priv(p, 1, priv - 1);
		ymc_match(p, ']');
		printf("index\n");
		goto accl;
	}
	goto done;
xDOT:
	if (cpl) parse_expr(p);
	if (ymc_peek(p) == '.') {
		ymc_match(p, '.');
		if (ymc_peek(p) == SYMBOL)
			printf("push %s\n", ymc_literal(p));
		ymc_match(p, SYMBOL);
		printf("index\n");
	}
	goto accl;
xCALL:
	if (cpl) parse_expr(p);
	if (ymc_peek(p) == '(') {
		int argc;
		ymc_match(p, '(');
		argc = parse_args(p);
		ymc_match(p, ')');
		printf("call %d\n", argc);
		goto accl;
	}
	goto done;
xUNQIUE:
	if (ymc_peek(p) == '-') {
		ymc_match(p, '-');
		parse_expr_priv(p, 1, priv - 1);
		printf("inv\n");
	}
	goto done;
xMUL:
	if (cpl) parse_expr(p);
	switch (ymc_peek(p)) {
	case '*':
		ymc_match(p, '*');
		parse_expr_priv(p, 1, priv - 1);
		printf("mul\n");
		goto done;
	case '/':
		ymc_match(p, '/');
		parse_expr_priv(p, 1, priv - 1);
		printf("div\n");
		goto done;
	default:
		goto done;
	}
xADD:
	if (cpl) parse_expr(p);
	switch (ymc_peek(p)) {
	case '+':
		ymc_match(p, '+');
		parse_expr_priv(p, 1, priv - 1);
		printf("add\n");
		goto done;
	case '-':
		ymc_match(p, '-');
		parse_expr_priv(p, 1, priv - 1);
		printf("sub\n");
		goto done;
	default:
		goto done;
	}
accl:
	if (ymc_peek(p) == '(')
		parse_expr_priv(p, 0, PRIV_CALL);
done:
	if (!ymc_term(ymc_peek(p)))
		parse_expr(p);
}

static void parse_partal(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '+':
	case '-':
		parse_expr_priv(p, 0, PRIV_ADD);
		break;
	case '*':
	case '/':
		parse_expr_priv(p, 0, PRIV_MUL);
		break;
	case '.':
		parse_expr_priv(p, 0, PRIV_DOT);
		break;
	case '[':
		parse_expr_priv(p, 0, PRIV_INDEX);
		break;
	case '(':
		parse_expr_priv(p, 0, PRIV_CALL);
		break;
	default:
		ymc_fail(p, "Error operator! %c", ymc_peek(p));
		break;
	}
	if (!ymc_term(ymc_peek(p)))
		parse_partal(p);
}

static void parse_expr(struct ymd_parser *p) {
	int token = ymc_peek(p);
	switch (token) {
	case DEC_LITERAL:
	case HEX_LITERAL:
	case SYMBOL:
		parse_expr_priv(p, 0, PRIV_LITERAL);
		break;
	case '-':
		parse_expr_priv(p, 0, PRIV_UNQIUE);
		break; // term
	case '(':
		parse_expr_priv(p, 0, PRIV_PAREN);
		break;
	/*case '{':
		parse_map(p, 0);
		break;
	case SKLS:
		parse_map(p, 1);
		break;*/
	}
	if (!ymc_term(ymc_peek(p)))
		parse_partal(p);
}

void ymc_compile(struct ymd_parser *p) {
	lex_next(&p->lex, &p->lah);
	if (setjmp(p->jpt))
		return;
	parse_expr(p);
}
