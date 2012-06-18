#include "lex.h"
#include <string.h>
#include <ctype.h>

#define DECL_RV \
	struct ytoken fake; \
	struct ytoken *rv = !x ? &fake : x; \
	memset(rv, 0, sizeof(*rv))

#define TERM_CHAR \
	     '+': case '*': case '/': case '%': case '^': case ',': \
	case '(': case ')': case '[': case ']': case '{': case '}': \
	case ':': case '.': case '&'
#define PREX_CHAR \
	     '-': case '<': case '>': case '=': case '!': case '~': \
	case '@': case '|'

static inline int lex_peek(struct ymd_lex *lex) {
	return !lex->buf[lex->off] ? EOS : lex->buf[lex->off];
}

static inline int lex_read(struct ymd_lex *lex) {
	int ch = lex->buf[lex->off++];
	++lex->i_column;
	return !ch ? EOS : ch;
}

static inline int lex_move(struct ymd_lex *lex) {
	int ch = lex->buf[lex->off];
	if (!ch) return EOS;
	ch = lex->buf[++lex->off];
	return !ch ? EOS : ch;
}

static inline int lex_token_c(struct ymd_lex *lex, struct ytoken *x) {
	char ch = lex->buf[lex->off];
	x->token = !ch ? EOS : ch; 
	x->off = lex->buf + lex->off;
	x->len = 1;
	return lex_read(lex);
}

static int lex_token_l(struct ymd_lex *lex, int m, int tk,
                              struct ytoken *x) {
	if (lex_move(lex) != m) {
		--lex->off;
		return lex_token_c(lex, x);
	}
	x->token = tk;
	x->off = lex->buf + lex->off - 1;
	x->len = 2;
	lex_move(lex);
	return x->token;
}

static int lex_token_lt(struct ymd_lex *lex, struct ytoken *x) {
	switch (lex->buf[lex->off + 1]) {
	case '=':
		return lex_token_l(lex, '=', LE, x);
	case '<':
		return lex_token_l(lex, '<', LSHIFT, x);
	}
	return lex_token_c(lex, x);
}

static int lex_token_gt(struct ymd_lex *lex, struct ytoken *x) {
	switch (lex->buf[lex->off + 1]) {
	case '=':
		return lex_token_l(lex, '=', GE, x);
	case '>':
		return lex_token_l(lex, '>', RSHIFT_A, x);
	}
	return lex_token_c(lex, x);
}

static int lex_term(int ch) {
	switch (ch) {
	case EOS:
	case TERM_CHAR:
	case PREX_CHAR:
		return 1;
	default:
		if (isspace(ch)) return 1;
		break;
	}
	return 0;
}

static int lex_read_dec(struct ymd_lex *lex, int neg, struct ytoken *x) {
	int ch;
	x->off = lex->buf + lex->off - neg;
	while (!lex_term(ch = lex_read(lex))) {
		if (isdigit(ch))
			++x->len;
		else
			return ERROR;
	}
	--lex->off;
	x->token = !x->len ? ERROR : DEC_LITERAL; 
	return x->token;
}

static int lex_read_hex(struct ymd_lex *lex, struct ytoken *x) {
	int ch;
	lex_move(lex);
	x->off = lex->buf + lex->off - 2; // '0x|0X' 2 char
	while (!lex_term(ch = lex_read(lex))) {
		if (isxdigit(ch))
			++x->len;
		else
			return ERROR;
	}
	--lex->off;
	x->token = !x->len ? ERROR : HEX_LITERAL; 
	return x->token;
}

int lex_next(struct ymd_lex *lex, struct ytoken *x) {
	DECL_RV;
	for (;;) {
		int ch = lex_peek(lex);
		switch (ch) {
		case EOS:
		case TERM_CHAR:
			return lex_token_c(lex, rv);
		case '-':
			if (isdigit(lex_move(lex)))
				return lex_read_dec(lex, 1, rv);
			--lex->off;
			return lex_token_c(lex, rv);
		case '<':
			return lex_token_lt(lex, rv);
		case '>':
			return lex_token_gt(lex, rv);
		case '|':
			return lex_token_l(lex, '>', RSHIFT_L, rv);
		case '!':
			return lex_token_l(lex, '=', NE, rv);
		case '~':
			return lex_token_l(lex, '=', MATCH, rv);
		case '=':
			return lex_token_l(lex, '=', EQ, rv);
		case '@':
			rv->off = lex->buf + lex->off;
			if (lex_move(lex) != '{')
				return ERROR;
			rv->token = SKLS;
			rv->len = 2;
			lex_move(lex);
			return rv->token;
		case '0':
			ch = lex_move(lex);
			if (ch == 'x' || ch == 'X')
				return lex_read_hex(lex, rv);
			--lex->off; // Fallback 1 position.
			return lex_read_dec(lex, 0, rv);
		case '\n':
			++lex->i_line; lex->i_column = -1;
			rv->token = EL;
			rv->off = lex->buf + lex->off;
			rv->len = 1;
			lex_move(lex);
			return rv->token;
		default:
			if (isspace(ch))
				lex_read(lex); // skip!
			else if (isdigit(ch))
				return lex_read_dec(lex, 0, rv);
			break;
		}
	}
	return ERROR;
}
