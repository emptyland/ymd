%{
#include "y.tab.h"
#include "symbol.h"
#include <stdio.h>
#include <string.h>
%}
%%
"nil" { return NIL; }
"true" { return TRUE; }
"false" { return FALSE; }
"typeof" { return TYPEOF; }
"var" { return VAR; }
"and" { return AND; }
"or" { return OR; }
"not" { return NOT; }
"func" { return FUNC; }
"return" { return RETURN; }
"with" { return WITH; }
"if" { return IF; }
"else" { return ELSE; }
"elif" { return ELIF; }
"for" { return FOR; }
"in" { return IN; }
"break" { return BREAK; }
"continue" { return CONTINUE; }
"." { return DOT; }
">" { return GT; }
">=" { return GE; }
"<" { return LT; }
"<=" { return LE; }
"==" { return EQ; }
"!=" { return NE; }
"=" { return ASSIGN; }
"-" { return SUB; }
"+" { return ADD; }
"*" { return MUL; }
"%" { return MOD; }
"^" { return POW; }
"/" { return DIV; }
"&" { return AND_BIT; }
"|" { return OR_BIT; }
"~" { return NOT_BIT; }
"~|" { return XOR_BIT; }
"<<" { return LSHIFT; }
">>" { return RSHIFT_ALG; }
"|>" { return RSHIFT_LOG; }
"(" { return LPAREN; }
")" { return RPAREN; }
"[" { return LBRACK; }
"]" { return RBRACK; }
"@{" { return OBRACE; }
"{" { return LBRACE; }
"}" { return RBRACE; }
"{}" { return EMAP; }
"{:}" { return EMAP; }
"{@}" { return ESKL; }
"{^}" { return EDYA; }
"," { return COMMA; }
":" { return COLON; }
"->" { return DICT; }
[0-9]+ {
	sym_push(yytext, -1);
	return LITERAL_DEC;
}
[0][x][0-9a-fA-F]{1,16} {
	sym_push(yytext, -1);
	return LITERAL_HEX;
}
[_a-zA-Z][a-zA-Z_0-9]* {
	sym_push(yytext, -1);
	return SYMBOL;
}
[\n] { return EL; }
[\t\r ]
[/][/].*[\n]
[#].*[\n]
["](([a-zA-Z~!@#$%^&*()_+-=`{\}[\]:;<>?,./\t\ ])|([\\]((["])|(x[0-9a-fA-F]{2})|(u[0-0a-fA-F]{4})|([\\])|([a])|([b])|([f])|([n])|([r])|([v])|([t])|([0]))))*["] {
	sym_push(yytext + 1, strlen(yytext) - 2);
	return STRING;
}
. { printf("Fatal: Error token!\n"); }
%%
int yywrap(void) {
	return 1;
}
