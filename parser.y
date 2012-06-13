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
#define I_ELEM 3
#if defined(DISASM)
#define DEMIT0(fmt)             printf("ASM: "fmt)
#define DEMIT1(fmt, arg1)       printf("ASM: "fmt, arg1)
#define DEMIT2(fmt, arg1, arg2) printf("ASM: "fmt, arg1, arg2)
#else
#define DEMIT0(fmt)
#define DEMIT1(fmt, arg1)
#define DEMIT2(fmt, arg1, arg2)
#endif
//static int index_access(const char *z);
static void emit_access(unsigned char inst, const char *z);
static void emit_bind(const char *z);
%}
%token NUMBER SYMBOL LBRACK RBRACK TRUE FALSE
%token EL NIL STRING FUNC COMMA EMAP ESKL EDYA
%token RETURN DICT FOR IN OBRACE ABRACE
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

file:
block {
	func_emit(sop(), emitAP(RET, 0));
}
;

block:
block stmt
| block func_decl EL
|
;

func_decl:
func_inittype begin EL block end {
	sop_pop();
}
;

func_inittype:
func SYMBOL lparen params rparen {
	int i = func_kz(sop_index(-2), sym_index(0), -1);
	func_emit(sop_index(-2), emitAfP(STORE, OFF, i));
	func_init(sop());
	sym_scope_end();
}
| VAR func SYMBOL lparen params rparen {
	int i = func_add_lz(sop_index(-2), sym_index(0));
	func_emit(sop_index(-2), emitAfP(STORE, LOCAL, i));
	func_init(sop());
	sym_scope_end();
}
;

closure_prototype:
func lparen params rparen {
	func_init(sop());
	sym_scope_end();
}
| func bind_decl lparen params rparen {
	func_init(sop());
	sym_scope_end();
}
;

func:
FUNC {
	sym_scope_begin();
	unsigned short id;
	struct func *fn = ymd_spawnf(&id);
	func_emit(sop(), emitAP(LOAD, id));
	sop_push(fn);
}
;

bind_decl:
lbrack bind_list rbrack {
	func_emit(sop_index(-2), emitAP(BIND, sop()->n_bind));
}
;

bind_list:
SYMBOL {
	emit_bind(sym_index(-1));
}
| bind_list COMMA SYMBOL {
	emit_bind(sym_index(-1));
}
;

params:
param COMMA params
| param
|
;

param:
SYMBOL {
	func_add_lz(sop(), sym_index(-1));
	++sop()->u.core->kargs;
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
| call {
	sym_pop();
}
| assign_stmt {
	sym_pop();
}
| if_stmt {
	sym_pop();
}
| decl_stmt
| with_stmt
| for_stmt
| break_stmt
| continue_stmt
| stmt EL
|
;

return_stmt:
return args endl {
	func_emit(sop(), emitAP(RET, sym_last_slot(I_ARGS, 0)));
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
	info_cond_back(sop());
	info_cond_pop();
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
	info_cond_push(sop()->u.core->kinst);
	func_emit(sop(), emitAf(JNE, UNDEF));
}
;

elif_test:
elif cond begin EL {
	info_cond_rcd('b', sop()->u.core->kinst);
	func_emit(sop(), emitAf(JNE, UNDEF));
}
;

else:
ELSE {
	info_cond_rcd('o', sop()->u.core->kinst);
	func_emit(sop(), emitAf(JMP, UNDEF));
	info_cond_rcd('b', sop()->u.core->kinst);
}
;

elif:
ELIF {
	info_cond_rcd('o', sop()->u.core->kinst);
	func_emit(sop(), emitAf(JMP, UNDEF));
	info_cond_rcd('b', sop()->u.core->kinst);
}
;

for_stmt:
for begin EL block end {
	func_emit(sop(), emitAfP(JMP, BACKWARD, info_loop_off(sop())));
	info_loop_back(sop(), 1);
	info_loop_pop();
}
| for_each block end {
	func_emit(sop(), emitAfP(JMP, BACKWARD, info_loop_off(sop())));
	info_loop_back(sop(), 0);
	info_loop_pop();
}
;

for_each:
for for_index COLON iter_call begin EL {
	info_loop_set_retry(sop()->u.core->kinst);
	emit_access(I_PUSH, info_loop_iter_name());
	func_emit(sop(), emitAP(CALL, 0));
	info_loop_set_jcond(sop()->u.core->kinst);
	func_emit(sop(), emitAf(FOREACH, UNDEF));
	emit_access(I_STORE, info_loop_id());
}
;

iter_call:
call {
	int i = func_add_lz(sop(), info_loop_iter_name());
	if (i < 0)
		i = func_find_lz(sop(), info_loop_iter_name());
	if (i < 0)
		yyerror("Can not find iterator local name");
	func_emit(sop(), emitAfP(STORE, LOCAL, i));
}
;

for_index:
SYMBOL {
	info_loop_set_id(sym_index(-1));
}
| VAR SYMBOL {
	func_add_lz(sop(), sym_index(-1));
	info_loop_set_id(sym_index(-1));
}
;

for:
FOR {
	info_loop_push(sop()->u.core->kinst);
}
;

break_stmt:
BREAK {
	info_loop_rcd('b', sop()->u.core->kinst);
	func_emit(sop(), emitAf(JMP, UNDEF));
}
;

continue_stmt:
CONTINUE {
	info_loop_rcd('c', sop()->u.core->kinst);
	func_emit(sop(), emitAf(JMP, UNDEF));
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
	sym_pop();
}
| SYMBOL ASSIGN rval {
	int i = func_add_lz(sop(), sym_index(0));
	func_emit(sop(), emitAfP(STORE, LOCAL, i));
	sym_pop();
}
;

assign_stmt:
lval_field ASSIGN rval {
	func_emit(sop(), emitAP(SETF, 1));
}
| SYMBOL ASSIGN rval {
	emit_access(I_STORE, sym_index(0));
}
;

lval_field:
accl DOT SYMBOL {
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
}
| accl LBRACK expr RBRACK
;

cond:
TRUE {
	func_emit(sop(), emitAfP(PUSH, BOOL, 1));
}
| FALSE {
	func_emit(sop(), emitAfP(PUSH, BOOL, 0));
}
| expr GT expr {
	func_emit(sop(), emitAf(TEST, GT));
}
| expr GE expr {
	func_emit(sop(), emitAf(TEST, GE));
}
| expr LT expr {
	func_emit(sop(), emitAf(TEST, LT));
}
| expr LE expr {
	func_emit(sop(), emitAf(TEST, LE));
}
| expr EQ expr {
	func_emit(sop(), emitAf(TEST, EQ));
}
| expr NE expr {
	func_emit(sop(), emitAf(TEST, NE));
}
| cond AND cond {
	func_emit(sop(), emitAf(LOGC, AND));
}
| cond OR cond {
	func_emit(sop(), emitAf(LOGC, OR));
}
| NOT cond {
	func_emit(sop(), emitAf(LOGC, NOT));
}
| id
| LPAREN cond RPAREN
;

expr:
number {
	// TODO:
	func_emit(sop(), emitAfP(PUSH, INT, atoi(sym_index(-1))));
}
| NIL {
	func_emit(sop(), emitAf(PUSH, NIL));
}
| STRING {
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
}
| closure_prototype begin EL block end {
	sop_pop();
}
| TYPEOF LPAREN rval RPAREN {
	func_emit(sop(), emitA(TYPEOF));
}
| map
| array
| accl
| calc
| LPAREN calc RPAREN
;

number:
LITERAL_DEC {
	// TODO:
}
| LITERAL_HEX {
	// TODO:
}
;

accl:
accl DOT SYMBOL {
	int i = func_kz(sop(), sym_index(-1), -1);
	func_emit(sop(), emitAfP(PUSH, ZSTR, i));
	func_emit(sop(), emitA(INDEX));
}
| expr LBRACK expr RBRACK {
	func_emit(sop(), emitA(INDEX));
}
| id
| call
;

calc:
SUB expr {
	func_emit(sop(), emitA(INV));
}
| expr MUL expr {
	func_emit(sop(), emitA(MUL));
}
| expr DIV expr {
	func_emit(sop(), emitA(DIV));
}
| expr ADD expr {
	func_emit(sop(), emitA(ADD));
}
| expr SUB expr {
	func_emit(sop(), emitA(SUB));
}
| expr MOD expr {
	func_emit(sop(), emitA(MOD));
}
| expr POW expr {
	func_emit(sop(), emitA(POW));
}
| expr AND_BIT expr {
	func_emit(sop(), emitA(ANDB));
}
| expr OR_BIT expr {
	func_emit(sop(), emitA(ORB));
}
| expr XOR_BIT expr {
	func_emit(sop(), emitA(XORB));
}
| NOT_BIT expr {
	func_emit(sop(), emitA(INVB));
}
| expr LSHIFT expr {
	func_emit(sop(), emitAf(SHIFT, LEFT));
}
| expr RSHIFT_ALG expr {
	func_emit(sop(), emitAf(SHIFT, RIGHT_A));
}
| expr RSHIFT_LOG expr {
	func_emit(sop(), emitAf(SHIFT, RIGHT_L));
}
;

call:
accl lparen args rparen {
	func_emit(sop(), emitAP(CALL, sym_last_slot(I_ARGS, 0)));
}
| id lparen args rparen {
	func_emit(sop(), emitAP(CALL, sym_last_slot(I_ARGS, 0)));
}
;

id:
SYMBOL {
	emit_access(I_PUSH, sym_index(-1));
}
;

args:
arg COMMA args
| arg
|
;

arg:
expr {
	sym_slot(I_ARGS, +1);
}
| cond {
	sym_slot(I_ARGS, +1);
}
;

array:
EDYA {
	func_emit(sop(), emitAP(NEWDYA, 0));
}
| begin elems end {
	func_emit(sop(), emitAP(NEWDYA, sym_last_slot(I_ELEM, 1)));
}
| begin EL elems end {
	func_emit(sop(), emitAP(NEWDYA, sym_last_slot(I_ELEM, 1)));
}
;

elems:
elem COMMA EL elems
| elem COMMA elems
| elem EL
| elem
;

elem:
expr {
	sym_slot(I_ELEM, +1);
}
| cond {
	sym_slot(I_ELEM, +1);
}
;

map:
EMAP {
	func_emit(sop(), emitAP(NEWMAP, 0));
}
| ESKL {
	func_emit(sop(), emitAP(NEWSKL, 0));
}
| begin EL def_list end {
	func_emit(sop(), emitAP(NEWMAP, sym_last_slot(I_DEFS, 1)));
}
| begin def_list end {
	func_emit(sop(), emitAP(NEWMAP, sym_last_slot(I_DEFS, 1)));
}
| ordered EL def_list end {
	func_emit(sop(), emitAP(NEWSKL, sym_last_slot(I_DEFS, 1)));
}
| ordered def_list end {
	func_emit(sop(), emitAP(NEWSKL, sym_last_slot(I_DEFS, 1)));
}
;

with_stmt:
expr WITH begin EL def_list end {
	func_emit(sop(), emitAP(SETF, sym_last_slot(I_DEFS, 1)));
}
;

def_list:
def COMMA EL def_list
| def COMMA def_list
| def EL
| def
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

ordered:
OBRACE {
	sym_scope_begin();
	sym_slot_begin();
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

/*
static int index_access(const char *z) {
	int i = func_find_lz(sop(), z);
	return i < 0 ? func_kz(sop(), z, -1) : i;
}*/

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
		func_emit(up, emitAfP(PUSH, OFF, k));
	} else {
		func_emit(up, emitAfP(PUSH, LOCAL, i));
	}
	++sop()->n_bind;
	func_add_lz(sop(), z); // FIXME
}
