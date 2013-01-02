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

// Define all tokens:
#define DECL_TOKEN(v) \
	v(NIL, "nil") \
	v(TRUE, "true") \
	v(FALSE, "false") \
	v(STRING, "<raw>") \
	v(RAW_STRING, "<raw>") \
	v(HEX_LITERAL, "<hex>") \
	v(DEC_LITERAL, "<dec>") \
	v(FLOAT, "<float>") \
	v(LE, "<") \
	v(GE, "<=") \
	v(NE, "!=") \
	v(MATCH, "~=") \
	v(EQ, "==") \
	v(SYMBOL, "<id>") \
	v(LSHIFT, "<<") \
	v(RSHIFT_A, ">>") \
	v(RSHIFT_L, "|>") \
	v(INC_1, "++") \
	v(DEC_1, "--") \
	v(INC, "+=") \
	v(DEC, "-=") \
	v(STRCAT, "..") \
	v(VAR, "var") \
	v(FUNC, "func") \
	v(AND, "and") \
	v(OR, "or") \
	v(NOT, "not") \
	v(IF, "if") \
	v(ELSE, "else") \
	v(ELIF, "elif") \
	v(WITH, "with") \
	v(FOR, "for") \
	v(RETURN, "return") \
	v(BREAK, "break") \
	v(CONTINUE, "continue") \
	v(TYPEOF, "typeof") \
	v(IN, "in") \
	v(FAIL, "fail") \
	v(LET, "let") \
	v(ARGV, "argv") \
	v(WHILE, "while")

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
	lex->i_line = 1;
}

// Get current line to buf.
const char *lex_line(struct ymd_lex *lex, char *buf, size_t n);
int lex_next(struct ymd_lex *lex, struct ytoken *rv);

#endif // YMD_LEX_H
