#include "decode.h"
#include "bytecode.h"
#include "tostring.h"
#include "value.h"
#include "value_inl.h"
#include <string.h>
#include <assert.h>

static const char *kz_test_op[] = {
	"eq", "ne", "gt", "ge", "lt", "le", "match",
};

static const char *kz_shift_op[] = {
	"left", "right:l", "right:a",
};

static const char *kz_calc_op[] = {
	"<error>", "inv", "mul", "div", "add", "sub", "mod", "andb", "orb",
	"xorb", "invb", "not",
};

static const char *kz_order[] = {
	"asc", "dasc", "user",
};

static const char *fn_kz(const struct func *fn, int i) {
	return kstr_k(fn->u.core->kval + i)->land;
}

static const char *fn_uz(const struct func *fn, int i) {
	return fn->u.core->uz[i]->land;
}

static const char *address(const struct func *fn, uint_t inst,
                           char *buf, size_t n) {
	switch (asm_flag(inst)) {
	case F_KVAL: {
		struct zostream os = ZOS_INIT;
		snprintf(buf, n, "%s",
		         tostring(&os, fn->u.core->kval + asm_param(inst)));
		zos_final(&os);
		} break;
	case F_LOCAL:
		snprintf(buf, n, "[local]:<%d>", asm_param(inst));
		break;
	case F_BOOL:
		snprintf(buf, n, asm_param(inst) == 0 ? "false" : "true");
		break;
	case F_NIL:
		strncpy(buf, "nil", n);
		break;
	case F_OFF:
		snprintf(buf, n, "[global]:@%s", fn_kz(fn, asm_param(inst)));
		break;
	case F_UP:
		snprintf(buf, n, "[upval]:@%s", fn_uz(fn, asm_param(inst)));
		break;
	case F_ARGV:
		strncpy(buf, "[argv]", n);
		break;
	default:
		assert(!"No reached.");
		return "<N/A>";
	}
	return buf;
}

static const char *jmp(uint_t inst, char *buf, size_t n) {
	switch (asm_flag(inst)) {
	case F_FORWARD:
		snprintf(buf, n, "+%d", asm_param(inst));
		break;
	case F_BACKWARD:
		snprintf(buf, n, "-%d", asm_param(inst));
		break;
	default:
		assert(!"No reached.");
		return "<N/A>";
	}
	return buf;
}

static const char *getf(const struct func *fn, uint_t inst,
                       char *buf, size_t n) {
	switch (asm_flag(inst)) {
	case F_STACK:
		snprintf(buf, n, "%d", asm_param(inst));
		break;
	case F_FAST:
		assert(asm_param(inst) < fn->u.core->kkval);
		snprintf(buf, n, "[%s]", fn_kz(fn, asm_param(inst)));
		break;
	default:
		assert(!"No reached.");
		return "<N/A>";
	}
	return buf;
}

const char *method(const struct func *fn, uint_t inst) {
	int i = asm_method(inst);
	return fn_kz(fn, i);
}

int dasm_inst(FILE *fp, const struct func *fn, uint_t inst) {
	char buf[128];
	int rv;
#define BUF buf, sizeof(buf)
	switch (asm_op(inst)) {
	case I_PANIC:
		rv = fprintf(fp, "panic");
		break;
	case I_SELFCALL:
		rv = fprintf(fp, "call [%s]:%d, ret:%d", method(fn, inst),
		             asm_argc(inst), asm_aret(inst));
		break;
	case I_STORE:
		rv = fprintf(fp, "store %s", address(fn, inst, BUF));
		break;  
	case I_INC:
		rv = fprintf(fp, "inc %s", address(fn, inst, BUF));
		break;
	case I_RET:
		rv = fprintf(fp, "ret %d", asm_param(inst));
		break;
	case I_JNE:
		rv = fprintf(fp, "jne %s", jmp(inst, BUF));
		break;
	case I_JNN:
		rv = fprintf(fp, "jnn %s", jmp(inst, BUF));
		break;
	case I_JNT:
		rv = fprintf(fp, "jnt %s", jmp(inst, BUF));
		break;
	case I_JMP:
		rv = fprintf(fp, "jmp %s", jmp(inst, BUF));
		break;
	case I_FOREACH:
		rv = fprintf(fp, "foreach %s", jmp(inst, BUF));
		break;
	case I_FORSTEP:
		rv = fprintf(fp, "forstep %s", jmp(inst, BUF));
		break;
	case I_SETF:
		rv = fprintf(fp, "setf %s", getf(fn, inst, BUF));
		break;
	case I_PUSH:
		rv = fprintf(fp, "push %s", address(fn, inst, BUF));
		break;
	case I_CLOSE:
		rv = fprintf(fp, "close %s", address(fn, inst, BUF));
		break;
	case I_TEST:
		rv = fprintf(fp, "test <%s>", kz_test_op[asm_flag(inst)]);
		break;
	case I_GETF:
		rv = fprintf(fp, "getf %s", getf(fn, inst, BUF));
		break;
	case I_TYPEOF:
		rv = fprintf(fp, "typeof");
		break;
	case I_CALC:
		rv = fprintf(fp, "%s", kz_calc_op[asm_flag(inst)]);
		break;
	case I_SHIFT:
		rv = fprintf(fp, "shift [%s]", kz_shift_op[asm_flag(inst)]);
		break;
	case I_CALL:
		rv = fprintf(fp, "call %d, ret:%d", asm_argc(inst), asm_aret(inst));
		break;
	case I_NEWMAP:
		rv = fprintf(fp, "newmap %d", asm_param(inst));
		break;
	case I_NEWSKL:
		rv = fprintf(fp, "newskl %d, [%s]", asm_param(inst),
				kz_order[asm_flag(inst)]);
		break;
	case I_NEWDYA:
		rv = fprintf(fp, "newdya %d", asm_param(inst));
		break;
	default:
		assert(0 && "No reached.");
		break;
	}
#undef BUF
	return rv;
}

int dasm_func(FILE *fp, const struct func *fn) {
	char buf[1024];
	int i, count = 0;
	if (fn->is_c)
		return -1;
	fprintf(fp, "----<%s>:\n", func_proto_z(fn, buf, sizeof(buf)));
	for (i = 0; i < fn->u.core->kinst; ++i) {
		fprintf(fp, "[%03d] ", i);
		count += dasm_inst(fp, fn, fn->u.core->inst[i]);
		fprintf(fp, "\n");
	}
	for (i = 0; i < fn->u.core->kkval; ++i) {
		if (ymd_type(&fn->u.core->kval[i]) == T_FUNC)
			count += dasm_func(fp, func_k(fn->u.core->kval + i));
	}
	return count;
}
