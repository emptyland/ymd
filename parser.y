%{
#include "y.tab.h"
#include "state.h"
#include "symbol.h"
#include "assembly.h"
#include <stdio.h>
#include <assert.h>
int yylex();
int yyerror(const char *e);
#define I_PARAMS 0
#define I_ARGS 1
#define I_DEFS 2
#if defined(DISASM)
#define DEMIT0(fmt)             printf("ASM: "fmt)
#define DEMIT1(fmt, arg1)       printf("ASM: "fmt, arg1)
#define DEMIT2(fmt, arg1, arg2) printf("ASM: "fmt, arg1, arg2)
#else
#define DEMIT0(fmt)
#define DEMIT1(fmt, arg1)
#define DEMIT2(fmt, arg1, arg2)
#endif
static void emit_access(unsigned char inst, const char *z);
static void emit_bind(const char *z);
%}
%token NUMBER SYMBOL LBRACK RBRACK TRUE FALSE
%token EL NIL STRING FUNC COMMA
%token RETURN DICT FOR IN OBRACE
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
	func_emit(sop(), emitAP(RET, 0));
	sop_pop();
	DEMIT0("fn_end\n");
}
;

func_prototype:
func SYMBOL lparen params rparen {
	int i = func_kz(sop(), sym_index(0), -1);
	func_emit(sop(), emitAfP(STORE, OFF, i));
	DEMIT1("store @%s\n", sym_index(0));
}
| VAR func SYMBOL lparen params rparen {
	int i = func_add_lz(sop(), sym_index(0));
	func_emit(sop(), emitAfP(STORE, LOCAL, i));
	DEMIT1("local %s\n", sym_index(0));
	DEMIT1("store @%s\n", sym_index(0));
}
;

closure_prototype:
func lparen params rparen
| func lbrack bind_list rbrack lparen params rparen
;

func:
FUNC {
	unsigned short id;
	struct func *fn = ymd_spawnf(&id);
	func_emit(sop(), emitAP(LOAD, id));
	sop_push(fn);
	DEMIT0("new_fn\n");
}
;

bind_list:
SYMBOL {
	emit_bind(sym_index(-1));
	DEMIT1("bind @%s\n", sym_index(-1));
}
| bind_list COMMA SYMBOL {
	emit_bind(sym_index(-1));
	DEMIT1("bind @%s\n", sym_index(-1));
}
;

params:
param COMMA params
| param
|
;

param:
SYMBOL {
	int i = func_add_lz(sop(), sym_index(-1));
	func_emit(sop(), emitAfP(STORE, LOCAL, i));
	DEMIT1("local %s\n", sym_index(-1));
	DEMIT1("store @%s\n", sym_index(-1));
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
	func_emit(sop(), emitAP(RET, sym_last_slot(I_ARGS, 0)));
	DEMIT1("ret %d\n", sym_last_slot(I_ARGS, 0));
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
	DEMIT0(".label_out:\n");
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
	DEMIT0("jne label_exit\n");
}
;

elif_test:
elif cond begin EL {
	DEMIT0("jne label_exit\n");
}
;

else:
ELSE {
	DEMIT0(".label_exit\n");	
}
;

elif:
ELIF {
	DEMIT0(".label_exit\n");
}
;

for_stmt:
for begin EL block end {
	func_emit(sop(), emitAfP(JMP, BACKWARD, sop_off()));
	DEMIT0("jmp label_loop\n");
	DEMIT0(".label_out\n");
}
| for_each block end {
	sop_fillback(1);
	func_emit(sop(), emitAfP(JMP, BACKWARD, sop_off()));
	DEMIT0("jmp label_loop\n");
	DEMIT0(".label_out\n");
}
;

for_each:
FOR for_index COLON call begin EL {
	//emit_access(I_PUSH, sym_index(0));
	push_off(sop()->n_inst);
	func_emit(sop(), emitAf(FOREACH, UNDEF));
	DEMIT0(".label_loop\n");
	DEMIT0("foreach label_out\n");
}
;

for_index:
SYMBOL {
	//emit_access(I_PUSH, sym_index(-1));
	//TODO:
}
| VAR SYMBOL {
	int i = func_add_lz(sop(), sym_index(-1));
	func_emit(sop(), emitAfP(PUSH, INT, i));
}
;

for:
FOR {
	push_off(sop()->n_inst);
	DEMIT0(".label_loop\n");
}
;

break_stmt:
BREAK {
	DEMIT0("jmp label_out\n");
}
;

continue_stmt:
CONTINUE {
	DEMIT0("jmp label_loop\n");
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
	func_add_lz(sop(), sym_index(0));
	DEMIT1("local %s\n", sym_index(0));
	sym_pop();
}
| SYMBOL ASSIGN rval {
	int i = func_add_lz(sop(), sym_index(0));
	func_emit(sop(), emitAfP(STORE, LOCAL, i));
	DEMIT1("local %s\n", sym_index(0));
	DEMIT1("store @%s\n", sym_index(0));
	sym_pop();
}
;

assign_stmt:
lval_field ASSIGN rval {
	func_emit(sop(), emitAP(SETF, 1));
	DEMIT0("setf 1\n");
	sym_pop();
}
| SYMBOL ASSIGN rval {
	emit_access(I_STORE, sym_index(0));
	DEMIT1("store @%s\n", sym_index(0));
	sym_pop();
}
;

lval_field:
accl DOT SYMBOL {
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
	DEMIT1("push '%s'\n", sym_index(-1));
}
| accl LBRACK expr RBRACK
;

cond:
TRUE {
	func_emit(sop(), emitAfP(PUSH, BOOL, 1));
	DEMIT0("push true\n");
}
| FALSE {
	func_emit(sop(), emitAfP(PUSH, BOOL, 0));
	DEMIT0("push false\n");
}
| expr GT expr {
	func_emit(sop(), emitAf(TEST, GT));
	DEMIT0("test gt\n");
}
| expr GE expr {
	func_emit(sop(), emitAf(TEST, GE));
	DEMIT0("test ge\n");
}
| expr LT expr {
	func_emit(sop(), emitAf(TEST, LT));
	DEMIT0("test lt\n");
}
| expr LE expr {
	func_emit(sop(), emitAf(TEST, LE));
	DEMIT0("test le\n");
}
| expr EQ expr {
	func_emit(sop(), emitAf(TEST, EQ));
	DEMIT0("test eq\n");
}
| expr NE expr {
	func_emit(sop(), emitAf(TEST, NE));
	DEMIT0("test ne\n");
}
| cond AND cond {
	func_emit(sop(), emitAf(LOGC, AND));
	DEMIT0("logc and\n");
}
| cond OR cond {
	func_emit(sop(), emitAf(LOGC, OR));
	DEMIT0("logc or\n");
}
| NOT cond {
	func_emit(sop(), emitAf(LOGC, NOT));
	DEMIT0("logc not\n");
}
| id
| LPAREN cond RPAREN
;

expr:
number {
	// TODO:
	func_emit(sop(), emitAfP(PUSH, INT, atoi(sym_index(-1))));
	DEMIT1("push %s\n", sym_index(-1));
}
| NIL {
	func_emit(sop(), emitAf(PUSH, NIL));
	DEMIT0("push nil\n");
}
| STRING {
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
	DEMIT1("push '%s'\n", sym_index(-1));
}
| closure_prototype begin EL block end {
	func_emit(sop(), emitAP(RET, 0));
	sop_pop();
	DEMIT0("fn_end\n");
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
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
	func_emit(sop(), emitA(INDEX));
	DEMIT1("push '%s'\n", sym_index(-1));
	DEMIT0("index\n");
}
| accl LBRACK expr RBRACK {
	func_emit(sop(), emitA(INDEX));
	DEMIT0("index\n");
}
| TYPEOF LPAREN rval RPAREN {
	func_emit(sop(), emitA(TYPEOF));
	DEMIT0("typeof\n");
}
| id
| call
;

calc:
SUB expr {
	func_emit(sop(), emitA(INV));
	DEMIT0("inv\n");
}
| expr MUL expr {
	func_emit(sop(), emitA(MUL));
	DEMIT0("mul\n");
}
| expr DIV expr {
	func_emit(sop(), emitA(DIV));
	DEMIT0("div\n");
}
| expr ADD expr {
	func_emit(sop(), emitA(ADD));
	DEMIT0("add\n");
}
| expr SUB expr {
	func_emit(sop(), emitA(SUB));
	DEMIT0("sub\n");
}
| expr MOD expr {
	func_emit(sop(), emitA(MOD));
	DEMIT0("mod\n");
}
| expr POW expr {
	func_emit(sop(), emitA(POW));
	DEMIT0("pow\n");
}
| expr AND_BIT expr {
	func_emit(sop(), emitA(ANDB));
	DEMIT0("andb\n");
}
| expr OR_BIT expr {
	func_emit(sop(), emitA(ORB));
	DEMIT0("orb\n");
}
| expr XOR_BIT expr {
	func_emit(sop(), emitA(XORB));
	DEMIT0("xorb\n");
}
| NOT_BIT expr {
	func_emit(sop(), emitA(INVB));
	DEMIT0("invb\n");
}
| expr LSHIFT expr {
	func_emit(sop(), emitAf(SHIFT, LEFT));
	DEMIT0("shift l\n");
}
| expr RSHIFT_ALG expr {
	func_emit(sop(), emitAf(SHIFT, RIGHT_A));
	DEMIT0("shift r, a\n");
}
| expr RSHIFT_LOG expr {
	func_emit(sop(), emitAf(SHIFT, RIGHT_L));
	DEMIT0("shift r, l\n");
}
;

call:
accl lparen args rparen {
	func_emit(sop(), emitAP(CALL, sym_last_slot(I_ARGS, 0)));
	DEMIT1("call %d\n", sym_last_slot(I_ARGS, 0));
}
| id lparen args rparen {
	func_emit(sop(), emitAP(CALL, sym_last_slot(I_ARGS, 0)));
	DEMIT1("call %d\n", sym_last_slot(I_ARGS, 0));
}
;

id:
SYMBOL {
	emit_access(I_PUSH, sym_index(-1));
	DEMIT1("push @%s\n", sym_index(-1));
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
	func_emit(sop(), emitAP(NEWMAP, sym_last_slot(I_DEFS, 1)));
	DEMIT1("newmap %d\n", sym_last_slot(I_DEFS, 1));
}
| begin def_list end {
	func_emit(sop(), emitAP(NEWMAP, sym_last_slot(I_DEFS, 1)));
	DEMIT1("newmap %d\n", sym_last_slot(I_DEFS, 1));
}
| ordered EL def_list end {
	func_emit(sop(), emitAP(NEWSKL, sym_last_slot(I_DEFS, 1)));
	DEMIT1("newskl %d\n", sym_last_slot(I_DEFS, 1));
}
| ordered def_list end {
	func_emit(sop(), emitAP(NEWSKL, sym_last_slot(I_DEFS, 1)));
	DEMIT1("newskl %d\n", sym_last_slot(I_DEFS, 1));
}
;

with_stmt:
expr WITH begin EL def_list end {
	func_emit(sop(), emitAP(SETF, sym_last_slot(I_DEFS, 1)));
	DEMIT1("setf %d\n", sym_last_slot(I_DEFS, 1));
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
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
	DEMIT1("push '%s'\n", sym_index(-1));
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

ordered:
OBRACE {
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
extern FILE *yyin;

int yyerror(const char *e) {
	char msg[260];
	snprintf(msg, sizeof(msg), "Fatal: %s\n", e);
	sop_error(msg, -1);
	return 0;
}

int do_compile(FILE *fp, struct func *fn) {
	yyin = fp;
	sop_push(fn);
	sym_scope_begin();
	sym_slot_begin();
	yyparse();
	sym_slot_end();
	sym_scope_end();
	sop_pop();
	return sop_result();
}

static void emit_access(unsigned char inst, const char *z) {
	int i = func_find_lz(sop(), z); 
	if (i < 0) {
		int k = func_kz(sop(), z, -1);
		func_emit(sop(), asm_build(inst, F_OFF, k));
	} else {
		func_emit(sop(), asm_build(inst, F_LOCAL, i));
	}
}

static void emit_bind(const char *z) {
	struct func *up = sop_index(-2);
	int i = func_find_lz(up, z);
	if (i < 0) {
		int k = func_kz(up, z, -1);
		func_emit(up, emitAfP(BIND, OFF, k));
	} else {
		func_emit(up, emitAfP(BIND, LOCAL, i));
	}
	func_add_lz(sop(), z); // FIXME
}
