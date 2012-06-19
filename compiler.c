#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline char *strndup(const char *p, size_t n) {
	char *rv = calloc(n + 1, 1);
	strncpy(rv, p, n);
	return rv;
}

static void parse_expr(struct ymd_parser *p);
static void parse_partal(struct ymd_parser *p);
static void parse_paren(struct ymd_parser *p);
static void parse_unique(struct ymd_parser *p);

static void ymc_fail(struct ymd_parser *p, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	longjmp(p->jpt, 1);
}

static int ymc_match(struct ymd_parser *p, int need) {
	if (need != p->lah.token)
		ymc_fail(p, "Error token; miss %c, need %c\n", p->lah.token,
		         need);
	return lex_next(&p->lex, &p->lah);
}

static inline
const struct ytoken *ymc_peek(struct ymd_parser *p) {
	return &p->lah;
}

static inline char *ymc_literal(struct ymd_parser *p) {
	return strndup(p->lah.off, p->lah.len);
}

static inline int ymc_term(int token) {
	switch (token) {
	case EL: case ERROR: case EOS: case ')':
		return 1;
	}
	return 0;
}

static void parse_literal(struct ymd_parser *p) {
	int token = ymc_peek(p)->token;
	switch (token) {
	case DEC_LITERAL: 
		printf("push %s\n", ymc_literal(p));
		ymc_match(p, token);
		break;
	case HEX_LITERAL:
		printf("push %s\n", ymc_literal(p));
		ymc_match(p, token);
		break;
	default:
		if (!ymc_term(ymc_peek(p)->token))
			parse_expr(p);
		break;
	}
}

static void parse_unique(struct ymd_parser *p) {
	switch (ymc_peek(p)->token) {
	case '-': ymc_match(p, '-');
		parse_literal(p);
		printf("inv\n");
		break;
	default:
		if (!ymc_term(ymc_peek(p)->token))
			parse_literal(p);
		break;
	}
}

static void parse_paren(struct ymd_parser *p) {
	switch (ymc_peek(p)->token) {
	case '(':
		ymc_match(p, '(');
		parse_expr(p);
		ymc_match(p, ')');
		break;
	default:
		if (!ymc_term(ymc_peek(p)->token))
			parse_unique(p);
		break;
	}
}

static void parse_mul(struct ymd_parser *p) {
	parse_unique(p);
	switch (ymc_peek(p)->token) {
	case '*': ymc_match(p, '*');
		parse_paren(p);
		printf("mul\n");
		break;
	case '/': ymc_match(p, '/');
		parse_paren(p);
		printf("div\n");
		break;
	default:
		if (!ymc_term(ymc_peek(p)->token))
			parse_paren(p);
		//if (ymc_peek(p)->token == ')')
		break;
	}
}

static void parse_partal(struct ymd_parser *p) {
	switch (ymc_peek(p)->token) {
	case '+': ymc_match(p, '+');
		parse_mul(p);
		printf("add\n");
		break;
	case '-': ymc_match(p, '+');
		parse_mul(p);
		printf("sub\n");
		break;
	case '*': ymc_match(p, '*');
		parse_paren(p);
		printf("mul\n");
		break;
	case '/': ymc_match(p, '/');
		parse_paren(p);
		printf("div\n");
		break;
	default:
		ymc_fail(p, "Error binary operator! %c\n", ymc_peek(p)->token);
		break;
	}
	if (!ymc_term(ymc_peek(p)->token))
		parse_partal(p);
}

static void parse_expr(struct ymd_parser *p) {
	int token = ymc_peek(p)->token;
	switch (token) {
	case DEC_LITERAL:
	case HEX_LITERAL:
		parse_literal(p);
		break;
	case '-':
		parse_unique(p);
		break; // term
	case '(':
		parse_paren(p);
		break;
	//default:
	//	ymc_fail(p, "Bad token!");
	//	break;
	}
	if (!ymc_term(ymc_peek(p)->token))
		parse_partal(p);
}

void ymc_compile(struct ymd_parser *p) {
	lex_next(&p->lex, &p->lah);
	if (setjmp(p->jpt))
		return;
	parse_expr(p);
}
