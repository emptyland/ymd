#ifndef YMD_ASSEMBLY_H
#define YMD_ASSEMBLY_H

// Instructions:
#define I_PANIC   0
#define I_SELFCALL 5 // selfcall a, n, "string"
#define I_STORE   10 // store local|off
#define I_RET     15 // ret n
#define I_JNE     20 // jne label
#define I_JMP     25 // jmp label
#define I_FOREACH 30 // foreach label
#define I_SETF    35 // setf stack|fast
#define I_PUSH    40 // push kval|true|false|nil|local|off
#define I_TEST    45 // test gt|ge|lt|le|eq|ne|match
#define I_JNT     50 // jnt label
#define I_JNN     55 // jnn label
#define I_TYPEOF  60 // typeof
#define I_INV     65
#define I_MUL     70
#define I_DIV     75
#define I_ADD     80
#define I_SUB     85
#define I_MOD     90
#define I_POW     95
#define I_ANDB    100
#define I_ORB     105
#define I_XORB    110
#define I_INVB    115
#define I_SHIFT   120 // shift l|r, a|l
#define I_CALL    125 // call a, n
#define I_NEWMAP  130 // newmap n
#define I_NEWSKL  135 // newskl
#define I_NEWDYA  140 // newdya
#define I_BIND    145 // bind n
// 150 reserved
#define I_GETF    155 // getf stack|fast
#define I_NOT     160 // not

// jne/jmp
#define F_FORWARD  0 // param: Number of instructions
#define F_BACKWARD 1 // param: Number of instructions
#define F_UNDEF    2 // param: Ignore

// push
#define F_KVAL    1 // param: `kval` offset
#define F_LOCAL   2 // param: `loc` offset
#define F_BOOL    3 // param: 0 true other false
#define F_NIL     4 // param: Ignore
#define F_OFF     5 // param: `kz` offset

// test
// param: Ignore all
#define F_EQ 0
#define F_NE 1
#define F_GT 2
#define F_GE 3
#define F_LT 4
#define F_LE 5
#define F_MATCH 6

// shift
// param: Ignore all
#define F_LEFT    0
#define F_RIGHT_L 1
#define F_RIGHT_A 2

// setf/getf
#define F_STACK 0 // param: number
#define F_FAST  1 // param: `kz` offset


typedef unsigned int   uint_t;
typedef unsigned char  uchar_t;
typedef unsigned short ushort_t;

static inline uint_t asm_build(
	uchar_t op,
	uchar_t flag,
	ushort_t param) {
	return ((uint_t)op) << 24 | ((uint_t)flag) << 16 | ((uint_t)param);
}

static inline uint_t asm_call(
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

static inline uchar_t asm_op(uint_t inst) {
	return (uchar_t)((inst & 0xff000000U) >> 24);
}

static inline uchar_t asm_flag(uint_t inst) {
	return (uchar_t)((inst & 0x00ff0000U) >> 16);
}

static inline ushort_t asm_param(uint_t inst) {
	return (ushort_t)(inst & 0x0000ffffU);
}

static inline uchar_t asm_aret(uint_t inst) {
	return (uchar_t)((asm_param(inst) & 0xf000U) >> 12);
}

static inline ushort_t asm_method(uint_t inst) {
	return (ushort_t)(asm_param(inst) & 0x0fffU);
}

static inline uchar_t asm_argc(uint_t inst) {
	return asm_flag(inst);
}

#endif // YMD_ASSEMBLY_H
