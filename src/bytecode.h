#ifndef YMD_ASSEMBLY_H
#define YMD_ASSEMBLY_H

#include "builtin.h"

// Instructions:
#define I_PANIC    0
#define I_SELFCALL 5 // selfcall a, n, "string"
#define I_STORE   10 // store local|up|off|index|field
#define I_RET     15 // ret n
#define I_JNE     20 // jne label
#define I_JMP     25 // jmp label
#define I_FOREACH 30 // foreach label
// reserved 35
#define I_PUSH    40 // push kval|true|false|nil|local|up|off|index|field
#define I_TEST    45 // test gt|ge|lt|le|eq|ne|match
#define I_JNT     50 // jnt label
#define I_JNN     55 // jnn label
#define I_TYPEOF  60 // typeof
#define I_CALC    65 // calc inv|mul|div|add|sub|mod|andb|orb|xorb|invb
#define I_CLOSE   70 // close kval
#define I_FORSTEP 75 // forstep label
#define I_INC     80 // inc local|up|off|index|field
#define I_DEC     85 // dec local|up|off|index|field
#define I_STRCAT  90 // strcat
// reserved 95~115
#define I_SHIFT   120 // shift l|r, a|l
#define I_CALL    125 // call a, n
#define I_NEWMAP  130 // newmap n
#define I_NEWSKL  135 // newskl order, n
#define I_NEWDYA  140 // newdya

// jne/jmp
#define F_FORWARD  0 // param: Number of instructions
#define F_BACKWARD 1 // param: Number of instructions
#define F_UNDEF    2 // param: Ignore

// push/store/inc/dec
#define F_KVAL    1 // param: `kval` offset
#define F_LOCAL   2 // param: `loc` offset
#define F_BOOL    3 // param: 0 true other false
#define F_NIL     4 // param: Ignore
#define F_OFF     5 // param: `kz` offset
#define F_UP      6 // param: `bind' offset closure's upval
#define F_ARGV    7 // param: Ignore
#define F_INDEX   8 // param: Ignore
#define F_FIELD   9 // param: `kz' offset

// test
// param: Ignore all
#define F_EQ 0    // ==
#define F_NE 1    // !=
#define F_GT 2    // >
#define F_GE 3    // >=
#define F_LT 4    // <
#define F_LE 5    // <=
#define F_MATCH 6 // ~=

// shift
// param: Ignore all
#define F_LEFT    0
#define F_RIGHT_L 1
#define F_RIGHT_A 2

// setf/getf
#define F_STACK 0 // param: number
#define F_FAST  1 // param: `kz` offset

// calc
#define F_INV    1 // -x
#define F_MUL    2 // a * b
#define F_DIV    3 // a / b
#define F_ADD    4 // a + b
#define F_SUB    5 // a - b
#define F_MOD    6 // a % b
#define F_ANDB   7 // a & b
#define F_ORB    8 // a | b
#define F_XORB   9 // a ^ b
#define F_INVB  10 // ~x
#define F_NOT   11 // not x

// skip list
#define F_ASC   0 // order by asc
#define F_DASC  1 // order by dasc
#define F_USER  2 // order by user defined function

static YMD_INLINE uint_t asm_build(
	uchar_t op,
	uchar_t flag,
	ushort_t param) {
	return ((uint_t)op) << 24 | ((uint_t)flag) << 16 | ((uint_t)param);
}

static YMD_INLINE uint_t asm_call(
	uchar_t op,
	uchar_t aret, // adjust return
	uchar_t argc,
	ushort_t method) {
	return asm_build(op, argc,
	                 ((aret & 0x0fU) << 12) | (method & 0x0fffU));
}

#define emitAfP(a, f, p) asm_build(I_##a, F_##f, p)
#define emitAf(a, f)     asm_build(I_##a, F_##f, 0)
#define emitAP(a, p)     asm_build(I_##a, 0, p)
#define emitA(a)         asm_build(I_##a, 0, 0)

#define asm_op(inst)    (((inst) & 0xff000000U) >> 24)
#define asm_flag(inst)  (((inst) & 0x00ff0000U) >> 16)
#define asm_param(inst) (((inst) & 0x0000ffffU))

#define asm_aret(inst)   ((asm_param(inst) & 0xf000U) >> 12)
#define asm_method(inst) (asm_param(inst) & 0x0fffU)
#define asm_argc(inst)   asm_flag(inst)

#endif // YMD_ASSEMBLY_H
