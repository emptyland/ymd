#include "lex.h"
#include "lex_test.def"

#define ASSERT_TOKEN(act) \
	rv = lex_next(&lex, &token); \
	ASSERT_EQ(int, rv, act); \
	ASSERT_EQ(int, rv, token.token)

static int test_sanity(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	(void)p;
	lex_init(&lex, NULL,
			"!~+- = ++ += -- -= <>/\n\t . ..%^*(){}[]:==>=<=!=~=>>|><<|&");
	ASSERT_TOKEN('!');
	ASSERT_TOKEN('~');
	ASSERT_TOKEN('+');
	ASSERT_TOKEN('-');
	ASSERT_TOKEN('=');
	ASSERT_TOKEN(INC_1);
	ASSERT_TOKEN(INC);
	ASSERT_TOKEN(DEC_1);
	ASSERT_TOKEN(DEC);
	ASSERT_TOKEN('<');
	ASSERT_TOKEN('>');
	ASSERT_TOKEN('/');
	ASSERT_TOKEN('.');
	ASSERT_TOKEN(STRCAT);
	ASSERT_TOKEN('%');
	ASSERT_TOKEN('^');
	ASSERT_TOKEN('*');
	ASSERT_TOKEN('(');
	ASSERT_TOKEN(')');
	ASSERT_TOKEN('{');
	ASSERT_TOKEN('}');
	ASSERT_TOKEN('[');
	ASSERT_TOKEN(']');
	ASSERT_TOKEN(':');
	ASSERT_TOKEN(EQ);
	ASSERT_TOKEN(GE);
	ASSERT_TOKEN(LE);
	ASSERT_TOKEN(NE);
	ASSERT_TOKEN(MATCH);
	ASSERT_TOKEN(RSHIFT_A);
	ASSERT_TOKEN(RSHIFT_L);
	ASSERT_TOKEN(LSHIFT);
	ASSERT_TOKEN('|');
	ASSERT_TOKEN('&');
	ASSERT_TOKEN(EOS);
	return 0;
}
#define ASSERT_DEC(num) \
	ASSERT_TOKEN(DEC_LITERAL); \
	ASSERT_EQ(int, 0, memcmp(#num, token.off, token.len))
static int test_lex_num_literal_1(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	(void)p;
	lex_init(&lex, NULL, "0 -1 -0 0123456789 9 1 12 123 1234");
	ASSERT_DEC(0);
	ASSERT_DEC(-1);
	ASSERT_DEC(-0);
	ASSERT_DEC(0123456789);
	ASSERT_DEC(9);
	ASSERT_DEC(1);
	ASSERT_DEC(12);
	ASSERT_DEC(123);
	ASSERT_DEC(1234);
	ASSERT_TOKEN(EOS);
	return 0;
}

#define ASSERT_HEX(num) \
	ASSERT_TOKEN(HEX_LITERAL); \
	ASSERT_EQ(int, 0, memcmp(#num, token.off, token.len))
static int test_lex_num_literal_2(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	(void)p;
	lex_init(&lex, NULL, "0x0\t0x1\t0x0123456789abcdefABCDEF");
	ASSERT_HEX(0x0);
	ASSERT_HEX(0x1);
	ASSERT_HEX(0x0123456789abcdefABCDEF);
	ASSERT_TOKEN(EOS);
	return 0;
}

#define ASSERT_FOT(num) \
	ASSERT_TOKEN(FLOAT); \
	ASSERT_EQ(int, sizeof(num) - 1, token.len); \
	ASSERT_EQ(int, 0, memcmp(num, token.off, token.len))
static int test_lex_num_literal_3(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	(void)p;
	lex_init(&lex, NULL, "0.1\t-0.1\t.1\t.001\t-.1\t3.1415927");
	ASSERT_FOT("0.1");
	ASSERT_FOT("-0.1");
	ASSERT_FOT(".1");
	ASSERT_FOT(".001");
	ASSERT_FOT("-.1");
	ASSERT_FOT("3.1415927");
	return 0;
}

#define ASSERT_SYM(sym) \
	ASSERT_TOKEN(SYMBOL); \
	ASSERT_EQ(int, 0, memcmp(#sym, token.off, token.len))
static int test_lex_sym_literal_1(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	(void)p;
	lex_init(&lex, NULL, " a\nb\n\t__\t_1\t_1abcdef\n(ab)");
	ASSERT_SYM(a);
	ASSERT_SYM(b);
	lex_init(&lex, NULL, "4 * (b + 2) / a\n");
	ASSERT_DEC(4);
	ASSERT_TOKEN('*');
	ASSERT_TOKEN('(');
	ASSERT_SYM(b);
	ASSERT_TOKEN('+');
	return 0;
}
#define ASSERT_KWD(kwd, tok) \
	ASSERT_TOKEN(tok); \
	ASSERT_EQ(int, 0, memcmp(#kwd, token.off, token.len))
static int test_lex_keyword(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	(void)p;
	lex_init(&lex, NULL, "nil true false and or func var let fail");
	ASSERT_KWD(nil, NIL);
	ASSERT_KWD(true, TRUE);
	ASSERT_KWD(false, FALSE);
	ASSERT_KWD(and, AND);
	ASSERT_KWD(or, OR);
	ASSERT_KWD(func, FUNC);
	ASSERT_KWD(var, VAR);
	ASSERT_KWD(let, LET);
	ASSERT_KWD(fail, FAIL);
	return 0;
}

#define ASSERT_RAW(str) \
	ASSERT_TOKEN(RAW_STRING); \
	ASSERT_EQ(int, 0, memcmp(str, token.off, token.len))
static int test_lex_raw_string(void *p) {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	lex_init(&lex, NULL, "\'\\d+\\w+\' \'\\s+\'");
	ASSERT_RAW("\\d+\\w+");
	ASSERT_RAW("\\s+");
	(void)p; return 0;
}

