%{
#include "y.tab.h"
#include <stdio.h>
extern int sym_count();
extern const char *sym_index(int i);
extern void sym_pop();
extern void sym_scope_begin();
extern void sym_scope_end();
extern void sym_slot_begin();
extern void sym_slot_end();
extern int sym_slot(int i, int op);
extern int sym_last_slot(int i, int lv);
int yylex();
int yyerror(const char *e);
#define I_PARAMS 0
#define I_ARGS 1
#define I_DEFS 2
%}
%token NUMBER SYMBOL LBRACK RBRACK TRUE FALSE
%token EL NIL STRING FUNC COMMA
%token RETURN DICT FOR IN
%token LBRACE RBRACE COLON WITH VAR
%token CONTINUE BREAK LITERAL_DEC LITERAL_HEX
%nonassoc IF ELSE ELIF
%right ASSIGN
%right TYPEOF
%left DOT
%left AND OR
%left NOT
%left LPAREN RPAREN
%left GT GE LT LE EQ NE
%left ADD SUB
%left MUL DIV MOD
%right POW
%left OR_BIT AND_BIT XOR_BIT
%left NOT_BIT
%left LSHIFT RSHIFT_ALG RSHIFT_LOG
%%

block:
block stmt
| block func_decl EL
|
;

func_decl:
func_prototype begin EL block end {
	printf("fn_end\n");
}
;

func_prototype:
func SYMBOL lparen params rparen {
	printf("store @%s\n", sym_index(0));
}
| VAR func SYMBOL lparen params rparen {
	printf("local %s\n", sym_index(0));
	printf("store @%s\n", sym_index(0));
}
;

closure_prototype:
func lparen params rparen
| func lbrack bind_list rbrack lparen params rparen
;

func:
FUNC {
	printf("newfn\n");
}
;

bind_list:
SYMBOL {
	printf("bind @%s\n", sym_index(-1));
}
| bind_list COMMA SYMBOL {
	printf("bind @%s\n", sym_index(-1));
}
;

params:
param COMMA params
| param
|
;

param:
SYMBOL {
	printf("local %s\n", sym_index(-1));
	printf("pop @%s\n", sym_index(-1));
	sym_slot(I_PARAMS, +1);
}
;

lbrack:
LBRACK {
	sym_scope_begin();
}
;

rbrack:
RBRACK {
	sym_scope_end();
}
;

stmt:
return_stmt
| call
| assign_stmt
| decl_stmt
| with_stmt
| if_stmt
| for_stmt
| break_stmt
| continue_stmt
| stmt EL
|
;

return_stmt:
return args endl {
	printf("ret %d\n", sym_last_slot(I_ARGS, 0));
}
;

return:
RETURN {
	sym_slot_begin();
}
;

endl:
EL {
	sym_slot_end();
}
;


if_stmt:
if_test block end elif_block else_block {
	printf(".label_out:\n");
	sym_pop();
}
;

else_block:
else begin EL block end
|
;

elif_block:
elif_test block end
| elif_block elif_test block end
|
;

if_test:
IF cond begin EL {
	printf("jne label_exit\n");
}
;

elif_test:
elif cond begin EL {
	printf("jne label_exit\n");
}
;

else:
ELSE {
	printf(".label_exit\n");	
}
;

elif:
ELIF {
	printf(".label_exit\n");
}
;

for_stmt:
for begin EL block end {
	printf("jmp label_loop\n");
	printf(".label_out\n");
}
| for_each block end {
	printf("jmp label_loop\n");
	printf(".label_out\n");
}
;

for_each:
FOR key COLON call begin EL {
	printf(".label_loop\n");
	printf("foreach label_out\n");
}
;

for:
FOR {
	printf(".label_loop\n");
}
;

break_stmt:
BREAK {
	printf("jmp label_out\n");
}
;

continue_stmt:
CONTINUE {
	printf("jmp label_loop\n");
}
;

decl_stmt:
VAR decl_list
;

decl_list:
decl_expr
| decl_list COMMA decl_expr
;

decl_expr:
SYMBOL {
	printf("local %s\n", sym_index(0));
	sym_pop(0);
}
| SYMBOL ASSIGN rval {
	printf("local %s\n", sym_index(0));
	printf("store @%s\n", sym_index(0));
	sym_pop(0);
}
;

assign_stmt:
lval_field ASSIGN rval {
	printf("setf 1\n");
	sym_pop();
}
| SYMBOL ASSIGN rval {
	printf("store @%s\n", sym_index(0));
	sym_pop();
}
;

lval_field:
accl DOT SYMBOL {
	printf("push '%s'\n", sym_index(-1));
}
| accl LBRACK expr RBRACK
;

cond:
TRUE {
	printf("push true\n");
}
| FALSE {
	printf("push false\n");
}
| expr GT expr {
	printf("test gt\n");
}
| expr GE expr {
	printf("test ge\n");
}
| expr LT expr {
	printf("test lt\n");
}
| expr LE expr {
	printf("test le\n");
}
| expr EQ expr {
	printf("test eq\n");
}
| expr NE expr {
	printf("test ne\n");
}
| cond AND cond {
	printf("logc and\n");
}
| cond OR cond {
	printf("logc or\n");
}
| NOT cond {
	printf("logc not\n");
}
| id
| LPAREN cond RPAREN
;

expr:
number {
	printf("push %s\n", sym_index(-1));
}
| NIL {
	printf("push nil\n");
}
| STRING {
	printf("push '%s'\n", sym_index(-1));
}
| closure_prototype begin EL block end {
	printf("fn_end\n");
}
| map
| accl
| calc
| LPAREN calc RPAREN
;

number:
LITERAL_DEC {
}
| LITERAL_HEX {
}
;

accl:
accl DOT SYMBOL {
	printf("push '%s'\n", sym_index(-1));
	printf("index\n");
}
| accl LBRACK expr RBRACK {
	printf("index\n");
}
| TYPEOF LPAREN rval RPAREN {
	printf("typeof\n");
}
| id
| call
;

calc:
SUB expr {
	printf("inv\n");
}
| expr MUL expr {
	printf("mul\n");
}
| expr DIV expr {
	printf("div\n");
}
| expr ADD expr {
	printf("add\n");
}
| expr SUB expr {
	printf("sub\n");
}
| expr MOD expr {
	printf("mod\n");
}
| expr POW expr {
	printf("pow\n");
}
| expr AND_BIT expr {
	printf("andb\n");
}
| expr OR_BIT expr {
	printf("orb\n");
}
| expr XOR_BIT expr {
	printf("xorb\n");
}
| NOT_BIT expr {
	printf("invb\n");
}
| expr LSHIFT expr {
	printf("shift l\n");
}
| expr RSHIFT_ALG expr {
	printf("shift r, a\n");
}
| expr RSHIFT_LOG expr {
	printf("shift r, l\n");
}
;

call:
accl lparen args rparen {
	printf("call %d\n", sym_last_slot(I_ARGS, 0));
}
| id lparen args rparen {
	printf("call %d\n", sym_last_slot(I_ARGS, 0));
}
;

id:
SYMBOL {
	printf("push @%s\n", sym_index(-1));
}
;

args:
expr COMMA args {
	sym_slot(I_ARGS, +1);
}
| cond COMMA args {
	sym_slot(I_ARGS, +1);
}
| expr {
	sym_slot(I_ARGS, +1);
}
| cond {
	sym_slot(I_ARGS, +1);
}
|
;

map:
begin EL def_list end {
	printf("newmap %d\n", sym_last_slot(I_DEFS, 1));
}
| begin def_list end {
	printf("newmap %d\n", sym_last_slot(I_DEFS, 1));
}
;

with_stmt:
expr WITH begin EL def_list end {
	printf("setf %d\n", sym_last_slot(I_DEFS, 1));
}
;

def_list:
def COMMA EL def_list
| def COMMA def_list
| def EL
| def
|
;

def:
key COLON rval {
	sym_slot(I_DEFS, +1);
}
| rval DICT rval {
	sym_slot(I_DEFS, +1);
}
;

key:
SYMBOL {
	printf("push '%s'\n", sym_index(-1));
}
;

rval:
cond
| expr
;

begin:
LBRACE {
	sym_scope_begin();
	sym_slot_begin();
}
;

end:
RBRACE {
	sym_slot_end();
	sym_scope_end();
}
;

lparen:
LPAREN {
	sym_slot_begin();
}
;

rparen:
RPAREN {
	sym_slot_end();
}
;
%%

int yyerror(const char *e) {
	printf("Fatal: %s\n", e);
}

int main() {
	sym_scope_begin();
	sym_slot_begin();
	yyparse();
	sym_slot_end();
	sym_scope_end();
	return 0;
}

