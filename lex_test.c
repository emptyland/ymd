#include "lex.h"
#include "yut.h"

#define ASSERT_TOKEN(act) \
	rv = lex_next(&lex, &token); \
	ASSERT_EQ(int, rv, act); \
	ASSERT_EQ(int, rv, token.token)
static int test_lex_token_1() {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	lex_init(&lex, NULL, "(2), \t\n (3- 11111)");

	ASSERT_TOKEN('(');
	ASSERT_TOKEN(DEC_LITERAL);
	ASSERT_TOKEN(')');
	ASSERT_TOKEN(',');
	ASSERT_TOKEN(EL);
	ASSERT_TOKEN('(');
	ASSERT_TOKEN(DEC_LITERAL);
	ASSERT_TOKEN('-');
	ASSERT_TOKEN(DEC_LITERAL);
	ASSERT_TOKEN(')');
	return 0;
}

static int test_lex_punc_1() {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	lex_init(&lex, NULL, "!~+-=<>/\n\t .%^*(){}[]:==>=<=!=~=>>|><<@{|&");
	ASSERT_TOKEN('!');
	ASSERT_TOKEN('~');
	ASSERT_TOKEN('+');
	ASSERT_TOKEN('-');
	ASSERT_TOKEN('=');
	ASSERT_TOKEN('<');
	ASSERT_TOKEN('>');
	ASSERT_TOKEN('/');
	ASSERT_TOKEN(EL);
	ASSERT_TOKEN('.');
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
	ASSERT_TOKEN(SKLS);
	ASSERT_TOKEN('|');
	ASSERT_TOKEN('&');
	ASSERT_TOKEN(EOS);
	return 0;
}
#define ASSERT_DEC(num) \
	ASSERT_TOKEN(DEC_LITERAL); \
	ASSERT_EQ(int, 0, memcmp(#num, token.off, token.len))
static int test_lex_num_literal_1() {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
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
static int test_lex_num_literal_2() {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	lex_init(&lex, NULL, "0x0\t0x1\t0x0123456789abcdefABCDEF");
	ASSERT_HEX(0x0);
	ASSERT_HEX(0x1);
	ASSERT_HEX(0x0123456789abcdefABCDEF);
	ASSERT_TOKEN(EOS);
	return 0;
}
#define ASSERT_SYM(sym) \
	ASSERT_TOKEN(SYMBOL); \
	ASSERT_EQ(int, 0, memcmp(#sym, token.off, token.len))
static int test_lex_sym_literal_1() {
	struct ymd_lex lex;
	struct ytoken token;
	int rv;
	lex_init(&lex, NULL, " a\nb\n\t__\t_1\t_1abcdef\n(ab)");
	ASSERT_SYM(a);
	ASSERT_TOKEN(EL);
	ASSERT_SYM(b);
	ASSERT_TOKEN(EL);
	lex_init(&lex, NULL, "4 * (b + 2) / a\n");
	ASSERT_DEC(4);
	ASSERT_TOKEN('*');
	ASSERT_TOKEN('(');
	ASSERT_SYM(b);
	ASSERT_TOKEN('+');
	return 0;
}

TEST_BEGIN
	TEST_ENTRY(lex_token_1, normal)
	TEST_ENTRY(lex_punc_1, normal)
	TEST_ENTRY(lex_num_literal_1, normal)
	TEST_ENTRY(lex_num_literal_2, normal)
	TEST_ENTRY(lex_sym_literal_1, normal)
TEST_END
