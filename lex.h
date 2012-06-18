#ifndef YMD_LEX_H
#define YMD_LEX_H

#include <string.h>

#if defined(TRUE)
#undef TRUE
#endif
#if defined(FALSE)
#undef FALSE
#endif
#if defined(ERROR)
#undef ERROR
#endif

enum ymd_token {
	EOS = -1, ERROR = 127,
	NIL, EL, TRUE, FALSE, STRING, HEX_LITERAL, DEC_LITERAL,
	LE, GE, NE, MATCH, EQ, SYMBOL, SKLS, LSHIFT, RSHIFT_A,
	RSHIFT_L,
};

struct ytoken {
	int token;
	const char *off;
	int len;
};

struct ymd_lex {
	const char *file;
	const char *buf;
	int off;
	int lah; // Look ahead
	int i_line;
	int i_column;
};

static inline void lex_init(struct ymd_lex *lex, const char *file,
                            const char *buf) {
	memset(lex, 0, sizeof(*lex));
	lex->file = file;
	lex->buf  = buf;
}

int lex_next(struct ymd_lex *lex, struct ytoken *rv);

#endif // YMD_LEX_H
