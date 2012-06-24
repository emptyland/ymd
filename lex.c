#include "lex.h"
#include <string.h>
#include <ctype.h>

#define DECL_RV \
	struct ytoken fake; \
	struct ytoken *rv = !x ? &fake : x; \
	memset(rv, 0, sizeof(*rv))

#define TERM_CHAR \
	     '+': case '*': case '%': case '^': case ',': case ';': \
	case '(': case ')': case '[': case ']': case '{': case '}': \
	case ':': case '.': case '&'
#define PREX_CHAR \
	     '-': case '<': case '>': case '=': case '!': case '~': \
	case '@': case '|': case '/': case '\"'

#define DEFINE_TOKEN(tok, literal) \
	{ sizeof(literal) - 1, literal, },
const struct {
	int len;
	const char *kz;
} tok_literal[] = {
	DECL_TOKEN(DEFINE_TOKEN)
};
#undef DEFINE_TOKEN

#define MAX_TOK ((int)(sizeof(tok_literal)/sizeof(tok_literal[0])))

static int lex_token(const struct ytoken *tok) {
	int i;
	if (!tok->len)
		return ERROR;
	for (i = 0; i < MAX_TOK; ++i) {
		if (tok->len == tok_literal[i].len &&
			memcmp(tok->off, tok_literal[i].kz, tok->len) == 0)
			break;
	}
	return i >= MAX_TOK ? SYMBOL : i + 128;
}

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
	++lex->i_column;
	return !ch ? EOS : ch;
}

static inline void lex_back(struct ymd_lex *lex) {
	--lex->off;
	--lex->i_column;
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
		lex_back(lex);
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
	x->token = ERROR;
	while (!lex_term(ch = lex_read(lex))) {
		if (isdigit(ch))
			++x->len;
		else
			return ERROR;
	}
	lex_back(lex);
	x->token = !x->len ? ERROR : DEC_LITERAL; 
	x->len += neg;
	return x->token;
}

static int lex_read_hex(struct ymd_lex *lex, struct ytoken *x) {
	int ch;
	lex_move(lex);
	x->token = ERROR;
	x->len = 2;
	x->off = lex->buf + lex->off - x->len; // '0x|0X' 2 char
	while (!lex_term(ch = lex_read(lex))) {
		if (isxdigit(ch))
			++x->len;
		else
			return ERROR;
	}
	lex_back(lex);
	x->token = !x->len ? ERROR : HEX_LITERAL; 
	return x->token;
}

static int lex_read_sym(struct ymd_lex *lex, struct ytoken *x) {
	x->token = ERROR;
	x->off = lex->buf + lex->off;
	for (;;) {
		int ch = lex_peek(lex);
		if (isalpha(ch) || isdigit(ch) || ch == '_')
			++x->len, lex_move(lex);
		else if (lex_term(ch))
			break;
		else
			return ERROR;
	}
	x->token = lex_token(x);
	return x->token;
}

static int lex_read_raw(struct ymd_lex *lex, struct ytoken *x) {
	lex_move(lex);
	x->token = ERROR;
	x->off = lex->buf + lex->off;
	for (;;) {
		int ch = lex_peek(lex);
		switch (ch) {
		case '\"':
			x->token = STRING;
			goto out;
		case '\\':
			lex_move(lex);
			lex_move(lex); x->len += 2; //Skip ESC char
			break;
		case '\n':
			return ERROR;
		default:
			lex_move(lex); ++x->len;
			break;
		}
	}
out:
	lex_move(lex);
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
		case '_':
			return lex_read_sym(lex, rv);
		case '\"':
			return lex_read_raw(lex, rv);
		case '-':
			lex_move(lex);
			if (isdigit(lex_peek(lex)))
				return lex_read_dec(lex, 1, rv);
			if (lex_peek(lex) == '>') {
				rv->off = lex->buf + lex->off - 2;
				rv->len = 2;
				rv->token = DICT;
				lex_move(lex);
				return rv->token;
			}
			lex_back(lex);
			return lex_token_c(lex, rv);
		case '/':
			if (lex_move(lex) == '/') {
				while (lex_move(lex) != '\n')
					;
				break;
			}
			lex_back(lex);
			return lex_token_c(lex, rv);
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
			lex_back(lex);
			return lex_read_dec(lex, 0, rv);
		case '\n':
			lex_move(lex);
			++lex->i_line;
			lex->i_column = 0;
			break;
		default:
			if (isspace(ch))
				lex_read(lex); // skip!
			else if (isdigit(ch))
				return lex_read_dec(lex, 0, rv);
			else if (isalpha(ch))
				return lex_read_sym(lex, rv);
			break;
		}
	}
	return ERROR;
}
