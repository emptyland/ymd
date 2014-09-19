/* ANSI-C code produced by gperf version 3.0.3 */
/* Command-line: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/gperf -L ANSI-C -C -N lex_keyword -K z -t -c -n keywords.gperf  */
/* Computed positions: -k'1-2,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 1 "keywords.gperf"

#include "lex.h"
typedef struct keyword keyword;
#line 5 "keywords.gperf"
struct keyword {
	const char *z;
	int token;
};

#define TOTAL_KEYWORDS 30
#define MIN_WORD_LENGTH 1
#define MAX_WORD_LENGTH 8
#define MIN_HASH_VALUE 0
#define MAX_HASH_VALUE 60
/* maximum key range = 61, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
hash (register const char *str, register unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61,  3, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      25,  5,  3,  0, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61,  3, 25,  3,
      25,  5,  0, 28,  0, 25,  1, 15, 10,  5,
       5, 30,  0, 61,  0,  8, 15, 61, 15, 13,
      61, 61, 30, 61,  0, 61, 15, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61, 61, 61, 61,
      61, 61, 61, 61, 61, 61, 61
    };
  register unsigned int hval = 0;

  switch (len)
    {
      default:
        hval += asso_values[(unsigned char)str[1]+1];
      /*FALLTHROUGH*/
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

const struct keyword *
lex_keyword (register const char *str, register unsigned int len)
{
  static const struct keyword wordlist[] =
    {
#line 30 "keywords.gperf"
      {"for", FOR},
      {"",0}, {"",0},
#line 20 "keywords.gperf"
      {"|>", RSHIFT_L},
      {"",0},
#line 31 "keywords.gperf"
      {"return", RETURN},
#line 19 "keywords.gperf"
      {">>", RSHIFT_A},
      {"",0},
#line 33 "keywords.gperf"
      {"continue", CONTINUE},
      {"",0},
#line 28 "keywords.gperf"
      {"elif", ELIF},
#line 15 "keywords.gperf"
      {"!=", NE},
      {"",0},
#line 17 "keywords.gperf"
      {"==", EQ},
#line 29 "keywords.gperf"
      {"with", WITH},
#line 27 "keywords.gperf"
      {"else", ELSE},
#line 10 "keywords.gperf"
      {"nil", NIL},
      {"",0},
#line 22 "keywords.gperf"
      {"func", FUNC},
      {"",0},
#line 25 "keywords.gperf"
      {"not", NOT},
      {"",0}, {"",0},
#line 16 "keywords.gperf"
      {"~=", MATCH},
      {"",0},
#line 37 "keywords.gperf"
      {"let", LET},
#line 38 "keywords.gperf"
      {"argv", ARGV},
      {"",0},
#line 11 "keywords.gperf"
      {"true", TRUE},
      {"",0},
#line 12 "keywords.gperf"
      {"false", FALSE},
      {"",0}, {"",0},
#line 14 "keywords.gperf"
      {"<=", GE},
      {"",0},
#line 36 "keywords.gperf"
      {"fail", FAIL},
      {"",0}, {"",0},
#line 24 "keywords.gperf"
      {"or", OR},
      {"",0},
#line 21 "keywords.gperf"
      {"var", VAR},
      {"",0}, {"",0},
#line 39 "keywords.gperf"
      {"while", WHILE},
      {"",0},
#line 34 "keywords.gperf"
      {"typeof", TYPEOF},
      {"",0}, {"",0},
#line 32 "keywords.gperf"
      {"break", BREAK},
      {"",0},
#line 13 "keywords.gperf"
      {"<", LE},
      {"",0}, {"",0},
#line 26 "keywords.gperf"
      {"if", IF},
      {"",0},
#line 18 "keywords.gperf"
      {"<<", LSHIFT},
      {"",0}, {"",0},
#line 23 "keywords.gperf"
      {"and", AND},
      {"",0},
#line 35 "keywords.gperf"
      {"in", IN}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      unsigned int key = hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          register const char *s = wordlist[key].z;

          if (*str == *s && !strncmp (str + 1, s + 1, len - 1) && s[len] == '\0')
            return &wordlist[key];
        }
    }
  return 0;
}
