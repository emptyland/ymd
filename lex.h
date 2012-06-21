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

#define DECL_TOKEN(v) \
	v(NIL, "nil") \
	v(EL, "\n") \
	v(TRUE, "true") \
	v(FALSE, "false") \
	v(STRING, "") \
	v(HEX_LITERAL, "") \
	v(DEC_LITERAL, "") \
	v(LE, "<") \
	v(GE, "<=") \
	v(NE, "!=") \
	v(MATCH, "~=") \
	v(EQ, "==") \
	v(SYMBOL, "") \
	v(SKLS, "@{") \
	v(LSHIFT, "<<") \
	v(RSHIFT_A, ">>") \
	v(RSHIFT_L, "|>") \
	v(DICT, "->") \
	v(VAR, "var") \
	v(FUNC, "func") \
	v(AND, "and") \
	v(OR, "or") \
	v(NOT, "not")

#define DEFINE_TOKEN(tok, literal) tok,
enum ymd_token {
	EOS = -1,
	ERROR = 127, // offset itoa
	DECL_TOKEN(DEFINE_TOKEN)
};
#undef DEFINE_TOKEN

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
