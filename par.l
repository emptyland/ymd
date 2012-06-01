%{
#include "y.tab.h"
#include <stdio.h>
#include <string.h>
static int sym_push(const char *z, int n);
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
"{" { return LBRACE; }
"}" { return RBRACE; }
"," { return COMMA; }
":" { return COLON; }
"->" { return DICT; }
[0-9]+ {
	sym_push(yytext, -1);
	return LITERAL_DEC;
}
[0][xX][0-9a-fA-F]+ {
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
["][a-zA-Z~!@#$%^&*()_+-=\\`{\}[\]:\";<>?,./\t\ ]*["] {
	sym_push(yytext + 1, strlen(yytext) - 2);
	return STRING;
}
. { printf("Fatal: Error token!\n"); }
%%
int yywrap(void) {
	return 1;
}

#define MAX_SLOT 16
#define MAX_STACK 128

struct slot {
	struct slot *prev;
	int elem[MAX_SLOT];
};
struct symbol {
	struct symbol *prev;
	struct slot *cnt;
	char *name[MAX_STACK];
	int n;
	int last_elem_s[MAX_SLOT];
	int last_elem[MAX_SLOT];
};
static struct symbol *sym;

static int sym_push(const char *z, int n) {
	if (n >= 0) {
		sym->name[sym->n] = malloc(n + 1);
		strncpy(sym->name[sym->n], z, n);
		sym->name[sym->n++][n] = 0;
	}
	else
		sym->name[sym->n++] = strdup(z);
	return sym->n - 1;
}
int sym_count() {
	return sym->n;
}
const char *sym_index(int i) {
	return i < 0 ? sym->name[sym->n + i] : sym->name[i];
}
void sym_pop() {
	int i = sym_count();
	while (i--) {
		free(sym->name[i]);
		sym->name[i] = NULL;
	}
	sym->n = 0;
}
void sym_scope_begin() {
	struct symbol *top = calloc(sizeof(struct symbol), 1);
	top->prev = sym;
	sym = top;
}
void sym_scope_end() {
	if (sym->prev)
		memcpy(sym->prev->last_elem_s,
		       sym->last_elem,
			   sizeof(sym->last_elem));
	sym_pop(0);
	struct symbol *tmp = sym;
	sym = sym->prev;
	free(tmp);
}
void sym_slot_begin() {
	struct slot *top = calloc(sizeof(struct slot), 1);
	top->prev = sym->cnt;
	sym->cnt = top;
	//printf("---slot begin----\n");
}
void sym_slot_end() {
	memcpy(sym->last_elem, sym->cnt->elem,
	       sizeof(sym->last_elem));
	struct slot *tmp = sym->cnt;
	sym->cnt = sym->cnt->prev;
	free(tmp);
	//printf("---slot end----\n");
}
int sym_slot(int i, int op) {
	sym->cnt->elem[i] += op;
	return sym->cnt->elem[i];
}
int sym_last_slot(int i, int lv) {
	switch (lv) {
	case 0:
		return sym->last_elem[i];
	case 1:
		return sym->last_elem_s[i];
	}
	return 0;
}
