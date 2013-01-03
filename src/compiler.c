#include "compiler.h"
#include "core.h"
#include "bytecode.h"
#include "encoding.h"
#include "print.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct znode {
	struct znode *chain;
	int len;
	char land[1];
};

struct loop_info {
	struct loop_info *chain;
	ushort_t i_retry;
	ushort_t i_jcond;
	ushort_t i_fail;
	uint_t jmt[128];  // jumping table
	int njmt;
	int death; // is death loop
	uchar_t op;
};

struct func_decl_desc {
	int method; // is method ?
	char name[260]; // func real name
	ushort_t load_p; // load point
	ushort_t id;
};

struct func_env {
	struct func_env *chain; // chain to prev env.
	struct chunk *core; // func block.
	struct hmap kval; // kval map.
};

static YMD_INLINE int ymc_peek(struct ymd_parser *p) {
	return p->lah.token;
}

static YMD_INLINE int ymc_next(struct ymd_parser *p) {
	return lex_next(&p->lex, &p->lah);
}

static YMD_INLINE int ymc_test(struct ymd_parser *p, int token) {
	if (ymc_peek(p) == token) {
		ymc_next(p);
		return 1;
	}
	return 0;
}

// Find or get `string' in constant list
static YMD_INLINE int ymk_kz(struct ymd_parser *p, const char *z, int n) {
	struct variable k;
	setv_kstr(&k, kstr_fetch(p->vm, z, n));
	return blk_kval(p->vm, p->env->core, &p->env->kval, &k);
}

// Find or put `int' in constant list
static YMD_INLINE int ymk_ki(struct ymd_parser *p, ymd_int_t i) {
	struct variable k;
	setv_int(&k, i);
	return blk_kval(p->vm, p->env->core, &p->env->kval, &k);
}

// Find or put `float' in constant list
static YMD_INLINE int ymk_kd(struct ymd_parser *p, ymd_float_t f) {
	struct variable k;
	setv_float(&k, f);
	return blk_kval(p->vm, p->env->core, &p->env->kval, &k);
}

// Find or put `function' in constant list
static YMD_INLINE int ymk_kf(struct ymd_parser *p, struct func *fn) {
	struct variable k;
	setv_func(&k, fn);
	return blk_kval(p->vm, p->env->core, &p->env->kval, &k);
}

static void ymc_fail(struct ymd_parser *p, const char *fmt, ...);
static int parse_expr(struct ymd_parser *p, int limit);
static void parse_stmt(struct ymd_parser *p);
static void parse_return(struct ymd_parser *p);
static void parse_closure(struct ymd_parser *p);
static void parse_block(struct ymd_parser *p);

//------------------------------------------------------------------------------
// Code generating
//------------------------------------------------------------------------------
static YMD_INLINE struct chunk *ymk_chunk(struct ymd_parser *p) {
	struct chunk *x = mm_zalloc(p->vm, 1, sizeof(*x));
	x->file = kstr_fetch(p->vm, p->lex.file, -1);
	return x;
}

static YMD_INLINE void ymk_chunk_reserved(struct ymd_parser *p,
                                      struct chunk *x) {
	(void)p;
	(void)x;
	// TODO:
}

static YMD_INLINE void ymk_enter(struct ymd_parser *p, struct chunk *x) {
	struct func_env *env = vm_zalloc(p->vm, sizeof(*env));
	env->core = !x ? ymk_chunk(p) : x;
	hmap_init(p->vm, &env->kval, 0);
	env->chain = p->env;
	p->env = env;
}

static YMD_INLINE struct chunk *ymk_leave(struct ymd_parser *p) {
	struct func_env *env = p->env;
	struct chunk *core = env->core;
	blk_shrink(p->vm, core); // Fixed chunk size
	p->env = env->chain;
	hmap_final(p->vm, &env->kval);
	vm_free(p->vm, env);
	return core;
}

static YMD_INLINE void ymk_emitOfP(
	struct ymd_parser *p, uchar_t o, uchar_t f, ushort_t param) {
	blk_emit(p->vm, p->env->core, asm_build(o, f, param), p->lex.i_line);
}
static YMD_INLINE void ymk_emitOf(
	struct ymd_parser *p, uchar_t o, uchar_t f) {
	ymk_emitOfP(p, o, f, 0);
}
static YMD_INLINE void ymk_emitO(
	struct ymd_parser *p, uchar_t o) {
	ymk_emitOf(p, o, 0);
}
static YMD_INLINE void ymk_emitOP(
	struct ymd_parser *p, uchar_t o, ushort_t param) {
	ymk_emitOfP(p, o, 0, param);
}
static YMD_INLINE void ymk_emit_call(
	struct ymd_parser *p, uchar_t o, uchar_t aret,
	uchar_t argc, ushort_t method) {
	blk_emit(p->vm, p->env->core, asm_call(o, aret, argc, method),
	         p->lex.i_line);
}

static YMD_INLINE ushort_t ymk_ipos(struct ymd_parser *p) {
	return p->env->core->kinst;
}

static YMD_INLINE ushort_t ymk_hold(struct ymd_parser *p) {
	ushort_t ipos = ymk_ipos(p);
	ymk_emitO(p, I_PANIC);
	return ipos;
}

static YMD_INLINE void ymk_emit_int(
	struct ymd_parser *p, ymd_int_t imm) {
	ymk_emitOfP(p, I_PUSH, F_KVAL, ymk_ki(p, imm));
}

static YMD_INLINE void ymk_emit_float(struct ymd_parser *p,
		ymd_float_t imm) {
	ymk_emitOfP(p, I_PUSH, F_KVAL, ymk_kd(p, imm));
}

static void ymk_emit_kz(struct ymd_parser *p, const char *raw) {
	char *priv = NULL;
	int i, k = stresc(raw, &priv);
	if (k < 0)
		ymc_fail(p, "Bad ESC string.");
	if (priv) {
		i = ymk_kz(p, priv, k);
		free(priv);
	} else {
		i = ymk_kz(p, "", 0);
	}
	ymk_emitOfP(p, I_PUSH, F_KVAL, i);
}

#define ymk_emit_rz(p) \
	ymk_emitOfP(p, I_PUSH, F_KVAL, ymk_kz(p, p->lah.off, p->lah.len))

static void ymk_emit_store_i(struct ymd_parser *p, int token,
		const char *symbol) {
	// Storing way:
	// 1. Local variable first
	int param = blk_find_lz(p->env->core, symbol);
	uchar_t op, flag;
	if (param >= 0) {
		flag = F_LOCAL;
		goto found;
	}
	// 2. Upval second
	param = blk_find_uz(p->env->core, symbol);
	if (param >= 0) {
		flag = F_UP;
		goto found;
	}
	// 3. Global third
	param = ymk_kz(p, symbol, -1);
	flag  = F_OFF;
found:
	switch (token) {
	case '=':
		op = I_STORE;
		break;
	case INC:
	case INC_1:
		op = I_INC;
		break;
	case DEC:
	case DEC_1:
		op = I_DEC;
		break;
	default:
		assert (!"No reached!");
		return;
	}
	if (token == INC_1 || token == DEC_1)
		ymk_emit_int(p, 1);
	ymk_emitOfP(p, op, flag, param);
}

#define ymk_emit_store(p, symbol) ymk_emit_store_i(p, '=', symbol)
#define ymk_emit_incr(p, symbol)  ymk_emit_store_i(p, INC, symbol)

static void ymk_emit_setf_i(struct ymd_parser *p, int token,
		const char *field) {
	ushort_t param = !field ? 0 : ymk_kz(p, field, -1);
	uchar_t op, flag = !field ? F_INDEX : F_FIELD;
	switch (token) {
	case '=':
		op    = I_STORE;
		param = flag == F_INDEX ? 1 : param;
		break;
	case INC:
	case INC_1:
		op    = I_INC;
		break;
	case DEC:
	case DEC_1:
		op    = I_DEC;
		break;
	default:
		assert (!"No reached!");
		return;
	}
	if (token == INC_1 || token == DEC_1)
		ymk_emit_int(p, 1);
	ymk_emitOfP(p, op, flag, param);
}

#define ymk_emit_setf(p, field) ymk_emit_setf_i(p, '=', field)

static void ymk_emit_push(
	struct ymd_parser *p, const char *symbol) {
	struct func_env *x = p->env;
	// Loading way:
	// 1. Local variable first
	int i = blk_find_lz(p->env->core, symbol);
	if (i >= 0) {
		ymk_emitOfP(p, I_PUSH, F_LOCAL, i);
		return;
	}
	// 2. Upval second
	i = blk_find_uz(p->env->core, symbol);
	if (i >= 0) {
		ymk_emitOfP(p, I_PUSH, F_UP, i);
		return;
	}
	// 2.5 Try find upval
	while ((x = x->chain) != NULL) {
		i = blk_find_lz(x->core, symbol); // upval is local?
		if (i < 0)
			i = blk_find_uz(x->core, symbol); // upval is upval?
		if (i >= 0) {
			struct func_env *link = p->env;
			while ((link = link->chain) != x) { // Link upval in every level.
				// FIXME: use better linking
				i = blk_add_uz(p->vm, link->core, symbol);
				assert (i >= 0 && "Duplicate linked upval.");
			}
			i = blk_add_uz(p->vm, p->env->core, symbol);
			ymk_emitOfP(p, I_PUSH, F_UP, i);
			return;
		}
	}
	// 3. Global varialbe third
	i = ymk_kz(p, symbol, -1);
	ymk_emitOfP(p, I_PUSH, F_OFF, i);
}

static void ymk_emit_pushz(
	struct ymd_parser *p, const char *symbol, int n) {
	int i = ymk_kz(p, symbol, n);
	ymk_emitOfP(p, I_PUSH, F_KVAL, i);
}

static int ymk_new_locvar(
	struct ymd_parser *p, const char *symbol) {
	int i = blk_find_uz(p->env->core, symbol);
	if (i >= 0) // Find symbol in upval
		ymc_fail(p, "Duplicate upval: %s", symbol);
	i = blk_add_lz(p->vm, p->env->core, symbol);
	if (i < 0) // Duplicate symbol in local
		ymc_fail(p, "Duplicate local varibale: `%s'", symbol);
	return i;
}

static void ymk_emit_end(struct ymd_parser *p) {
	struct chunk *core = p->env->core;
	if (!core->kinst || asm_op(core->inst[core->kinst - 1]) != I_RET)
		ymk_emitOP(p, I_RET, 0);
}

#define ymk_emit_getf(p, symbol) \
	ymk_emitOfP(p, I_PUSH, F_FIELD, ymk_kz(p, symbol, -1))

static void ymk_emit_selfcall(
	struct ymd_parser *p, int adjust, int argc, const char *method) {
	int i = ymk_kz(p, method, -1);
	ymk_emit_call(p, I_SELFCALL, adjust, argc, i);
}

static YMD_INLINE void ymk_emit_jmp(struct ymd_parser *p, uint_t target,
                                int bwd) {
	ushort_t off;
	const uint_t i_curr = p->env->core->kinst;
	if (bwd) {
		assert(i_curr >= target);
		off = i_curr - target;
	} else {
		assert(i_curr <= target);
		off = target - i_curr;
	}
	ymk_emitOfP(p, I_JMP, bwd ? F_BACKWARD : F_FORWARD, off);
}

static YMD_INLINE void ymk_hack(struct ymd_parser *p, ushort_t i, uint_t inst) {
	const struct chunk *core = p->env->core;
	assert(i < core->kinst);
	core->inst[i] = inst;
}

static YMD_INLINE void ymk_hack_jmp(
	struct ymd_parser *p, ushort_t i, uchar_t op, int bwd) {
	const struct chunk *core = p->env->core;
	ushort_t off = core->kinst - i;
	assert(core->kinst >= i);
	ymk_hack(p, i, asm_build(op, bwd ? F_BACKWARD : F_FORWARD, off));
}

// Jmp to fail block
static YMD_INLINE void ymk_fail_jmp(struct ymd_parser *p, ushort_t i_fail,
		ushort_t i, uchar_t op) {
	const struct chunk *core = p->env->core;
	ushort_t off = i_fail - i;
	assert(core->kinst >= i); (void)core;
	ymk_hack(p, i, asm_build(op, F_FORWARD, off));
}

static YMD_INLINE void ymk_emit_func(
	struct ymd_parser *p,
	const struct func_decl_desc *desc,
	struct func *fn) {
	int i = ymk_kf(p, fn);
	if (fn->u.core->kuz) {
		fn->n_upval = fn->u.core->kuz;
		ymk_hack(p, desc->load_p, emitAfP(CLOSE, KVAL, i));
	} else {
		ymk_hack(p, desc->load_p, emitAfP(PUSH, KVAL, i));
	}
}

static YMD_INLINE char *ymk_literal(struct ymd_parser *p) {
	struct znode *x = vm_zalloc(p->vm, sizeof(*x) + p->lah.len);
	x->len = p->lah.len;
	memcpy(x->land, p->lah.off, x->len);
	x->chain = p->lnk;
	p->lnk = x;
	return x->land;
}

static const char *ymk_symbol(struct ymd_parser *p) {
	const char *literal;
	if (ymc_peek(p) != SYMBOL)
		ymc_fail(p, "Error token, need symbol.");
	literal = ymk_literal(p);
	ymc_next(p);
	return literal;
}

static YMD_INLINE void ymk_loop_enter(
	struct ymd_parser *p, struct loop_info *info) {
	memset(info, 0, sizeof(*info));
	info->chain = p->loop;
	p->loop = info;
}

static void ymk_loop_leave(struct ymd_parser *p) {
	int i;
	const uint_t i_curr = p->env->core->kinst;
	assert(p->loop != NULL);
	assert(p->loop->i_retry <= i_curr); (void)i_curr;
	// Jump back
	ymk_emit_jmp(p, p->loop->i_retry, 1);
	// Has `fail' block?
	if (ymc_peek(p) == FAIL) {
		ymc_next(p); // Skip `fail'
		p->loop->i_fail = ymk_ipos(p);
		parse_block(p);
	}
	// Fillback breack statements
	i = p->loop->njmt;
	if (p->loop->i_fail > 0) {
		while (i--) {
			const uint_t i_jpt = p->loop->jmt[i];
			assert(i_jpt <= p->loop->i_fail);
			ymk_fail_jmp(p, p->loop->i_fail, i_jpt, I_JMP);
		}
	} else {
		while (i--) {
			const uint_t i_jpt = p->loop->jmt[i];
			assert(i_jpt <= i_curr);
			ymk_hack_jmp(p, i_jpt, I_JMP, 0);
		}
	}
	// Fillback foreach
	if (!p->loop->death)
		ymk_hack_jmp(p, p->loop->i_jcond, p->loop->op, 0);
	p->loop = p->loop->chain;
}

//------------------------------------------------------------------------------
// Compiling
//------------------------------------------------------------------------------
// Token to string
static const struct {
	int len;
	const char *kz;
} kttoa[] = {
#define DEFINE_TOKEN(tok, name) \
	{ sizeof(name) - 1, name, },
DECL_TOKEN(DEFINE_TOKEN)
#undef DEFINE_TOKEN
};
static const char *ymc_ttoa(int token, char buf[2]) {
	size_t i;
	if (token == EOS)
		return "eof";
	if (token == ERROR)
		return "<token error!>";
	if (token < ERROR) {
		buf[0] = (char)token;
		buf[1] = 0;
		return buf;
	}
	i = token - ERROR - 1;
	assert(i < sizeof(kttoa)/sizeof(kttoa[0]));
	return kttoa[i].kz;
}


// Error information like this:
// foo.ymd:1:1: fatal: Unexpected symbol.
// var i = j
//         ^
//
static void ymc_fail(struct ymd_parser *p, const char *fmt, ...) {
	va_list ap;
	int i = p->lex.i_column > 0 ? p->lex.i_column - 1 : 0;
	char msg[1024];
	// Format error message.
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	// Error output:
	ymd_printf("%s:%d:%d: ${[red]fatal:}$ %s\n",
			!p->lex.file ? "[none]" : p->lex.file,
			p->lex.i_line,
			p->lex.i_column,
			msg);
	// Show error position:
	puts(lex_line(&p->lex, msg, sizeof(msg)));
	while (i--)
		putchar(' ');
	ymd_printf("${[green]^}$\n");
	longjmp(p->jpt, 1);
}

static int ymc_match(struct ymd_parser *p, int need) {
	char exp[2], unexp[2];
	if (need != p->lah.token)
		ymc_fail(p, "Error token! unexpected `%s', expected `%s'",
		         ymc_ttoa(p->lah.token, exp),
		         ymc_ttoa(need, unexp));
	return lex_next(&p->lex, &p->lah);
}

static void parse_array(struct ymd_parser *p) {
	ushort_t count = 0;
	ymc_match(p, '[');
	do {
		switch (ymc_peek(p)) {
		case ']':
			goto out;
		default:
			parse_expr(p, 0);
			++count;
			break;
		}
	} while (ymc_test(p, ','));
out:
	ymk_emitOP(p, I_NEWDYA, count);
}

static void parse_map(struct ymd_parser *p, int ord) {
	ushort_t count = 0;
	uchar_t order = F_ASC;
	if (ord) { // is skip list
		ymc_match(p, '@');
		if (ymc_peek(p) == '[') {
			ymc_next(p);
			switch (ymc_peek(p)) {
			case '<':
				ymc_next(p);
				order = F_ASC;
				break;
			case '>':
				ymc_next(p);
				order = F_DASC;
				break;
			default:
				// Order by user defined function
				parse_expr(p, 0);
				order = F_USER;
				break;
			}
			ymc_match(p, ']');
		}
	}
	ymc_match(p, '{');
	do {
		switch (ymc_peek(p)) {
		case '}':
			goto out;
		case SYMBOL:
			ymk_emit_pushz(p, ymk_symbol(p), -1);
			ymc_match(p, ':');
			parse_expr(p, 0);
			++count;
			break;
		case STRING:
			ymk_emit_kz(p, ymk_literal(p));
			ymc_next(p);
			ymc_match(p, ':');
			parse_expr(p, 0);
			++count;
			break;
		case RAW_STRING:
			ymk_emit_rz(p);
			ymc_next(p);
			ymc_match(p, ':');
			parse_expr(p, 0);
			++count;
			break;
		default:
			ymc_match(p, '[');
			parse_expr(p, 0);
			ymc_match(p, ']');
			ymc_match(p, '=');
			parse_expr(p, 0);
			++count;
			break;
		}
	} while(ymc_test(p, ','));
out:
	if (ord)
		ymk_emitOfP(p, I_NEWSKL, order, count);
	else
		ymk_emitOP(p, I_NEWMAP, count);
}

static int parse_args(struct ymd_parser *p) {
	int nargs = 1;
	parse_expr(p, 0);
	while (ymc_test(p, ',')) {
		parse_expr(p, 0);
		++nargs;
	}
	return nargs;
}

static void parse_callargs(struct ymd_parser *p, int adjust,
                           const char *method) {
	ushort_t nargs = 0;
	switch (ymc_peek(p)) {
	case '(':
		ymc_next(p);
		if (ymc_peek(p) == ')') {
			ymc_next(p);
			goto out;
		}
		nargs += parse_args(p);
		ymc_match(p, ')');
		break;
	case STRING:
		ymk_emit_kz(p, ymk_literal(p));
		ymc_next(p);
		nargs += 1;
		break;
	case RAW_STRING:
		ymk_emit_rz(p);
		ymc_next(p);
		nargs += 1;
		break;
	default:
		ymc_fail(p, "Function arguments expected.");
		break;
	}
out:
	if (!method)
		ymk_emit_call(p, I_CALL, adjust, nargs, 0);
	else
		ymk_emit_selfcall(p, adjust, nargs, method);
}

static void parse_primary(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '(':
		ymc_next(p);
		parse_expr(p, 0);
		ymc_match(p, ')');
		break;
	case SYMBOL:
		ymk_emit_push(p, ymk_symbol(p));
		break;
	case ARGV:
		ymc_next(p);
		ymk_emitOf(p, I_PUSH, F_ARGV);
		++(p->env->core->argv); // Record argv number of referenced.
		break;
	default:
		ymc_fail(p, "Unexpected symbol");
		break;
	}
}

static void parse_suffixed(struct ymd_parser *p) {
	parse_primary(p);
	for (;;) {
		switch (ymc_peek(p)) {
		case '.':
			ymc_next(p);
			ymk_emit_getf(p, ymk_symbol(p));
			break;
		case '[':
			ymc_next(p);
			parse_expr(p, 0);
			ymc_match(p, ']');
			ymk_emitOfP(p, I_PUSH, F_INDEX, 1);
			break;
		case ':': {
			const char *method;
			ymc_next(p);
			method = ymk_symbol(p);
			parse_callargs(p, 1, method);
			} break;
		case '(': case STRING: case RAW_STRING:
			parse_callargs(p, 1, NULL);
			break;
		default:
			return;
		}
	}
}

static void parse_simple(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case NIL:
		ymk_emitOf(p, I_PUSH, F_NIL);
		break;
	case TRUE:
		ymk_emitOfP(p, I_PUSH, F_BOOL, 1);
		break;
	case FALSE:
		ymk_emitOfP(p, I_PUSH, F_BOOL, 0);
		break;
	case DEC_LITERAL: {
		int ok = 1;
		ymk_emit_int(p, dtoll(ymk_literal(p), &ok));
		assert(ok);
		} break;
	case HEX_LITERAL: {
		int ok = 1;
		ymk_emit_int(p, xtoll(ymk_literal(p), &ok));
		assert(ok);
		} break;
	case FLOAT: {
		int ok = 1;
		ymk_emit_float(p, ltof(ymk_literal(p), &ok));
		assert(ok);
		} break;
	case STRING:
		ymk_emit_kz(p, ymk_literal(p));
		break;
	case RAW_STRING:
		ymk_emit_rz(p);
		break;
	case '{':
		parse_map(p, 0);
		break;
	case '@':
		parse_map(p, 1);
		break;
	case '[':
		parse_array(p);
		break;
	case FUNC:
		parse_closure(p);
		return;
	default:
		parse_suffixed(p);
		return;
	}
	ymc_next(p);
}

static const struct {
	int left;
	int right;
} prio[] = {
	{9, 9}, {9, 9}, {10, 10}, {10, 10}, {10, 10}, // + - * / %
	{7, 7}, {7, 7}, {7, 7}, // << |> >>
	{6, 6}, {6, 6}, {6, 6}, // == != ~=
	{6, 6}, {6, 6}, {6, 6}, {6, 6}, // < <= > >=
	{3, 3}, {4, 4}, {5, 5}, // | ^ &
	{2, 2}, {1, 1}, {1, 1}, // and or ..
};
#define PRIO_UNARY 11

enum ymd_op {
	OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
	OP_LSHIFT, OP_RSHIFT_L, OP_RSHIFT_A,
	OP_EQ, OP_NE, OP_MATCH,
	OP_LT, OP_LE, OP_GT, OP_GE,
	OP_ORB, OP_ANDB, OP_XORB,
	OP_AND, OP_OR, OP_STRCAT,
	OP_NOT_BINARY,
	OP_MINS,
	OP_NOT,
	OP_NOT_NIL,
	OP_TYPEOF,
	OP_INVB,
	OP_NOT_UNARY,
};

static int ymc_unary_op(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '-': return OP_MINS;
	case NOT: return OP_NOT;
	case '!': return OP_NOT_NIL;
	case TYPEOF: return OP_TYPEOF;
	case '~': return OP_INVB;
	default: return OP_NOT_UNARY;
	}
	return 0;
}

static int ymc_binary_op(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case '+': return OP_ADD;
	case '-': return OP_SUB;
	case '*': return OP_MUL;
	case '/': return OP_DIV;
	case '%': return OP_MOD;
	case EQ: return OP_EQ;
	case NE: return OP_NE;
	case MATCH: return OP_MATCH;
	case '<': return OP_LT;
	case LE: return OP_LE;
	case '>': return OP_GT;
	case GE: return OP_GE;
	case AND: return OP_AND;
	case OR: return OP_OR;
	case STRCAT: return OP_STRCAT;
	case '|': return OP_ORB;
	case '&': return OP_ANDB;
	case '^': return OP_XORB;
	case LSHIFT: return OP_LSHIFT;
	case RSHIFT_A: return OP_RSHIFT_A;
	case RSHIFT_L: return OP_RSHIFT_L;
	default: return OP_NOT_BINARY;
	}
}

static void ymk_unary(struct ymd_parser *p, int op) {
	(void)p;
	switch (op) {
	case OP_MINS:
		ymk_emitOf(p, I_CALC, F_INV);
		break;
	case OP_NOT:
	case OP_NOT_NIL:
		ymk_emitOf(p, I_CALC, F_NOT);
		break;
	case OP_INVB:
		ymk_emitOf(p, I_CALC, F_INVB);
		break;
	case OP_TYPEOF:
		ymk_emitO(p, I_TYPEOF);
		break;
	default:
		assert(!"No reached.");
		break;
	}
}

static ushort_t ymk_hold_logc(struct ymd_parser *p, int op) {
	switch (op) {
	case OP_AND:
	case OP_OR:
		return ymk_hold(p);
	default:
		break;
	}
	return 0;
}

static void ymk_binary(struct ymd_parser *p, int op, ushort_t ipos) {
	(void)p;
	switch (op) {
	case OP_ADD:
		ymk_emitOf(p, I_CALC, F_ADD);
		break;
	case OP_SUB:
		ymk_emitOf(p, I_CALC, F_SUB);
		break;
	case OP_MUL:
		ymk_emitOf(p, I_CALC, F_MUL);
		break;
	case OP_DIV:
		ymk_emitOf(p, I_CALC, F_DIV);
		break;
	case OP_MOD:
		ymk_emitOf(p, I_CALC, F_MOD);
		break;
	case OP_ORB:
		ymk_emitOf(p, I_CALC, F_ORB);
		break;
	case OP_ANDB:
		ymk_emitOf(p, I_CALC, F_ANDB);
		break;
	case OP_XORB:
		ymk_emitOf(p, I_CALC, F_XORB);
		break;
	case OP_LSHIFT:
		ymk_emitOf(p, I_SHIFT, F_LEFT);
		break;
	case OP_RSHIFT_L:
		ymk_emitOf(p, I_SHIFT, F_RIGHT_L);
		break;
	case OP_RSHIFT_A:
		ymk_emitOf(p, I_SHIFT, F_RIGHT_A);
		break;
	case OP_EQ:
		ymk_emitOf(p, I_TEST, F_EQ);
		break;
	case OP_NE:
		ymk_emitOf(p, I_TEST, F_NE);
		break;
	case OP_MATCH:
		ymk_emitOf(p, I_TEST, F_MATCH);
		break;
	case OP_LT:
		ymk_emitOf(p, I_TEST, F_LT);
		break;
	case OP_LE:
		ymk_emitOf(p, I_TEST, F_LE);
		break;
	case OP_GT:
		ymk_emitOf(p, I_TEST, F_GT);
		break;
	case OP_GE:
		ymk_emitOf(p, I_TEST, F_GE);
		break;
	case OP_AND:
		ymk_hack_jmp(p, ipos, I_JNT, 0);
		break;
	case OP_OR:
		ymk_hack_jmp(p, ipos, I_JNN, 0);
		break;
	case OP_STRCAT:
		ymk_emitO(p, I_STRCAT);
		break;
	default:
		assert(!"No reached.");
		break;
	}
}

static int parse_expr(struct ymd_parser *p, int limit) {
	int op = ymc_unary_op(p);
	if (op != OP_NOT_UNARY) {
		ymc_next(p);
		parse_expr(p, PRIO_UNARY);
		ymk_unary(p, op);
	} else
		parse_simple(p);
	op = ymc_binary_op(p);
	while (op != OP_NOT_BINARY && prio[op].left > limit) {
		int next_op;
		ushort_t i;
		ymc_next(p);
		i = ymk_hold_logc(p, op);
		next_op = parse_expr(p, prio[op].right);
		ymk_binary(p, op, i);
		op = next_op;
	}
	return op;
}

enum {
	VSYMBOL,
	VDOT,
	VINDEX,
	VCALL,
};

struct lval_desc {
	const char *last;
	int vt; // value type
};

static void ymk_lval_partial(struct ymd_parser *p,
                            const struct lval_desc *desc) {
	(void)p;
	switch (desc->vt) {
	case VSYMBOL:
		ymk_emit_push(p, desc->last);
		break;
	case VINDEX:
		ymk_emitOfP(p, I_PUSH, F_INDEX, 1);
		break;
	case VDOT:
		ymk_emit_getf(p, desc->last);
		break;
	default:
		break;
	}
}

static void ymk_lval_assign(struct ymd_parser *p,
		const struct lval_desc *desc,
		int op) {
	switch (desc->vt) {
	case VSYMBOL:
		ymk_emit_store_i(p, op, desc->last);
		break;
	case VINDEX:
		ymk_emit_setf_i(p, op, NULL);
		break;
	case VDOT:
		ymk_emit_setf_i(p, op, desc->last);
		break;
	default:
		ymc_fail(p, "Syntax error, call is not lval.");
		break;
	}
}

static void parse_lval(struct ymd_parser *p, struct lval_desc *desc) {
	switch (ymc_peek(p)) {
	case SYMBOL:
		desc->last = ymk_symbol(p);
		desc->vt = VSYMBOL;
		break;
	default:
		ymc_fail(p, "Unexpected symbol.");
		break;
	}
	for (;;) {
		switch (ymc_peek(p)) {
		case '.':
			ymk_lval_partial(p, desc);
			ymc_next(p);
			desc->last = ymk_symbol(p);
			desc->vt = VDOT;
			break;
		case '[':
			ymk_lval_partial(p, desc);
			ymc_next(p);
			parse_expr(p, 0);
			ymc_match(p, ']');
			desc->last = NULL;
			desc->vt = VINDEX;
			break;
		case ':':
			ymk_lval_partial(p, desc);
			ymc_next(p);
			desc->last = ymk_symbol(p);
			parse_callargs(p, 0, desc->last);
			desc->last = NULL;
			desc->vt = VCALL;
			break;
		case '(': case STRING: case RAW_STRING:
			ymk_lval_partial(p, desc);
			parse_callargs(p, 0, NULL);
			desc->last = NULL;
			desc->vt = VCALL;
			break;
		default:
			return;
		}
	}
}

static void parse_expr_stat(struct ymd_parser *p) {
	struct lval_desc desc;
	int op;
	parse_lval(p, &desc);
	op = ymc_peek(p);
	switch (op) {
	case '=':
	case INC:
	case DEC:
		ymc_next(p);
		parse_expr(p, 0);
		ymk_lval_assign(p, &desc, op);
		break;
	case INC_1:
	case DEC_1:
		ymc_next(p);
		ymk_lval_assign(p, &desc, op);
		break;
	default:
		if (desc.vt != VCALL)
			ymc_fail(p, "Syntax error.");
		break;
	}
}

static void parse_params(struct ymd_parser *p, int method) {
	int nparam = 0;
	ymc_match(p, '(');
	if (method) {
		ymk_new_locvar(p, "self");
		++nparam;
	}
	do {
		switch (ymc_peek(p)) {
		case ')':
			goto out;
		case SYMBOL:
			ymk_new_locvar(p, ymk_symbol(p));
			++nparam;
			break;
		default:
			ymc_fail(p, "Bad param declare.");
			return;
		}
	} while (ymc_test(p, ','));
out:
	ymc_match(p, ')');
	p->env->core->kargs = nparam;
}

static void parse_block(struct ymd_parser *p) {
	ymc_match(p, '{');
	while (ymc_peek(p) != '}')
		parse_stmt(p);
	ymc_next(p);
}

static void parse_func_decl(struct ymd_parser *p, const char *prefix,
                            int local, struct func_decl_desc *desc) {
	memset(desc, 0, sizeof(*desc));
	strcpy(desc->name, prefix); // FIXME: strncpy
	if (ymc_peek(p) == '.') {
		ymk_emit_push(p, prefix);
		prefix = NULL;
		do {
			if (prefix)
				ymk_emit_getf(p, prefix);
			ymc_match(p, '.');
			prefix = ymk_symbol(p);
			strcat(desc->name, ".");
			strcat(desc->name, prefix);
		} while (ymc_peek(p) != '(');
		desc->load_p = ymk_hold(p);
		ymk_emit_setf(p, prefix);
		desc->method = 1;
	} else {
		desc->load_p = ymk_hold(p);
		if (local)
			ymk_new_locvar(p, prefix);
		ymk_emit_store(p, prefix);
		desc->method = 0;
	}
}

static YMD_INLINE struct chunk *fbk(struct ymd_context *l) {
	struct func *fn = func_of(l, ymd_top(l, 0));
	return fn->u.core;
}

static void parse_func(struct ymd_parser *p, int local) {
	struct ymd_context *l = ioslate(p->vm);
	struct func_decl_desc desc;
	ymc_next(p);
	if (ymc_peek(p) != SYMBOL)
		ymc_fail(p, "Function need a name.");
	parse_func_decl(p, ymk_symbol(p), local, &desc);
	ymd_func(l, ymk_chunk(p), desc.name);
	ymk_enter(p, fbk(l));
		parse_params(p, desc.method);
		ymk_chunk_reserved(p, fbk(l));
		parse_block(p);
		ymk_emit_end(p);
	ymk_leave(p);
	ymk_emit_func(p, &desc, func_of(l, ymd_top(l, 0)));
	ymd_pop(l, 1);
}

static void parse_closure(struct ymd_parser *p) {
	struct ymd_context *l = ioslate(p->vm);
	struct func_decl_desc desc;
	ymc_next(p);
	desc.load_p = ymk_hold(p);
	snprintf(desc.name, sizeof(desc.name), "__blk_xl%d__",
	         p->lex.i_line);
	ymd_func(l, ymk_chunk(p), desc.name);
	ymk_enter(p, fbk(l));
		parse_params(p, 0);
		ymk_chunk_reserved(p, fbk(l));
		parse_block(p);
		ymk_emit_end(p);
	ymk_leave(p);
	ymk_emit_func(p, &desc, func_of(l, ymd_top(l, 0)));
	ymd_pop(l, 1);
}

static void parse_return(struct ymd_parser *p) {
	ymc_next(p); // Skip `return'
	switch (ymc_peek(p)) {
	case ';':
	case '}':
		ymk_emitOP(p, I_RET, 0);
		break;
	default:
		parse_expr(p, 0);
		ymk_emitOP(p, I_RET, 1);
		break;
	}
}

static void parse_decl(struct ymd_parser *p) {
	int i = ymk_new_locvar(p, ymk_symbol(p));
	if (ymc_peek(p) != '=')
		return;
	ymc_next(p);
	parse_expr(p, 0);
	ymk_emitOfP(p, I_STORE, F_LOCAL, i);
}

static void parse_local(struct ymd_parser *p) {
	ymc_next(p);
	if (ymc_peek(p) == FUNC) {
		parse_func(p, 1);
		return;
	}
	parse_decl(p);
	while (ymc_test(p, ',')) parse_decl(p);
}

static void parse_if(struct ymd_parser *p) {
	ushort_t jnxt, jout[128];
	int i = 0;
	ymc_next(p); // Skip `if'
	if (ymc_peek(p) == LET) {
		ymc_next(p); // `if' `let' assign `;' cond
		parse_expr_stat(p);
		ymc_match(p, ';');
	} else if (ymc_peek(p) == VAR) {
		// Like `let' syntax
		parse_local(p); // `if' `var' expr `;' cond
		ymc_match(p, ';');
	}
	parse_expr(p, 0); // `if' expr
	jnxt = ymk_hold(p);
	parse_block(p);
	while (ymc_peek(p) == ELIF) { // `elif' expr { `block' }
		ymc_next(p);
		jout[i++] = ymk_hold(p);
		ymk_hack_jmp(p, jnxt, I_JNE, 0); jnxt = 0;
		parse_expr(p, 0);
		jnxt = ymk_hold(p);
		parse_block(p);
	}
	if (ymc_peek(p) == ELSE) { // `else' `{' block `}'
		ymc_next(p);
		jout[i++] = ymk_hold(p);
		ymk_hack_jmp(p, jnxt, I_JNE, 0); jnxt = 0;
		parse_block(p);
	}
	while (i--) ymk_hack_jmp(p, jout[i], I_JMP, 0);
	if (jnxt) // Is jnxt fill back finished?
		ymk_hack_jmp(p, jnxt, I_JNE, 0);
}

static void parse_forstep_partial(struct ymd_parser *p, const char *tmp) {
	char step[64], end[64];
	// Skip `='
	ymc_match(p, '=');
	// Initial expression:
	parse_expr(p, 0);
	ymk_emit_store(p, tmp);
	ymc_match(p, ',');
	// The end variable
	parse_expr(p, 0);
	snprintf(end, sizeof(end), "__loop_end_%d__", p->for_id);
	ymk_new_locvar(p, end);
	ymk_emit_store(p, end);
	// The step variable
	snprintf(step, sizeof(step), "__loop_step_%d__", p->for_id);
	ymk_new_locvar(p, step);
	if (ymc_peek(p) == ',') {
		ymc_next(p);
		parse_expr(p, 0);
		ymk_emit_store(p, step);
	} else {
		ymk_emit_int(p, 1LL); // Default step is: 1
		ymk_emit_store(p, step);
	}
	p->loop->i_retry = ymk_ipos(p); // set retry
	// Push i:
	ymk_emit_push(p, tmp);
	// Push end:
	ymk_emit_push(p, end);
	// Push step:
	ymk_emit_push(p, step);
	// Set jcond for instruction
	p->loop->i_jcond = ymk_hold(p);
	p->for_id++;
	// `{' block `}'
	parse_block(p); 
	// Incrment i variable: tmp = tmp + step
	ymk_emit_push(p, step);
	ymk_emit_incr(p, tmp);
	// Operator
	p->loop->op = I_FORSTEP;
}

static void parse_foreach_partial(struct ymd_parser *p, const char *tmp) {
	char iter[64];
	// `in' token:
	ymc_match(p, IN);
	parse_expr(p, 0);
	// Push iterator variable:
	snprintf(iter, sizeof(iter), "__loop_iter_%d__", p->for_id++);
	ymk_emitOfP(p, I_STORE, F_LOCAL, ymk_new_locvar(p, iter));
	// Save loop entry:
	p->loop->i_retry = ymk_ipos(p);
	// Push then call iterator:
	ymk_emit_push(p, iter);
	ymk_emit_call(p, I_CALL, 1, 0, 0);
	// Set jcond for foreach instruction:
	p->loop->i_jcond = ymk_hold(p);
	// Store i variable
	ymk_emit_store(p, tmp);
	// `{' block `}'
	parse_block(p);
	// Operator
	p->loop->op = I_FOREACH;
}

static YMD_INLINE void parse_for_partial(struct ymd_parser *p, const char *tmp) {
	if (ymc_peek(p) == '=')
		parse_forstep_partial(p, tmp);
	else
		parse_foreach_partial(p, tmp);
}

static void parse_for(struct ymd_parser *p) {
	const char *tmp;
	struct loop_info scope;
	ymc_next(p); // `for' `symbol' : `call'
	ymk_loop_enter(p, &scope);
	switch (ymc_peek(p)) {
	case '{':
		scope.death = 1;
		p->loop->i_retry = ymk_ipos(p); // set retry
		parse_block(p);
		break;
	case VAR:
		ymc_next(p);
		tmp = ymk_symbol(p);
		ymk_new_locvar(p, tmp);
		parse_for_partial(p, tmp);
		break;
	case SYMBOL:
		tmp = ymk_symbol(p);
		parse_for_partial(p, tmp);
		break;
	default:
		ymc_fail(p, "Syntax error.");
		return;
	}
	ymk_loop_leave(p);
}

// while_stmt ::= `while' expr `{' block `}'
// while_stmt ::= `while' `let' assign `;' expr `{' block `}'
// while_stmt ::= `while' `var' init `;' expr `{' block `}'
static void parse_while(struct ymd_parser *p) {
	struct loop_info scope;
	ymc_next(p);
	ymk_loop_enter(p, &scope);
	p->loop->op = I_JNE;
	switch (ymc_peek(p)) {
	case LET:
		ymc_next(p);
		p->loop->i_retry = ymk_ipos(p);
		parse_expr_stat(p);
		ymc_match(p, ';');
		break;
	case VAR:
		parse_local(p);
		p->loop->i_retry = ymk_ipos(p);
		ymc_match(p, ';');
		break;
	default:
		p->loop->i_retry = ymk_ipos(p);
		break;
	}
	parse_expr(p, 0);
	p->loop->i_jcond = ymk_hold(p);
	parse_block(p);
	ymk_loop_leave(p);
}

static void parse_goto(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case BREAK:
		p->loop->jmt[p->loop->njmt++] = ymk_hold(p);
		break;
	case CONTINUE:
		ymk_emit_jmp(p, p->loop->i_retry, 1);
		break;
	}
	ymc_next(p);
}

static void parse_stmt(struct ymd_parser *p) {
	switch (ymc_peek(p)) {
	case ';':
		ymc_next(p); // Empty statement
		break;
	case IF:
		parse_if(p);
		break;
	//TODO: case WITH:
	//	break;
	case FOR:
		parse_for(p);
		break;
	case WHILE:
		parse_while(p);
		break;
	case FUNC:
		parse_func(p, 0);
		break;
	case VAR:
		parse_local(p);
		break;
	case RETURN:
		parse_return(p);
		break;
	case BREAK:
	case CONTINUE:
		parse_goto(p);
		break;
	default:
		parse_expr_stat(p);
		break;
	}
}

static void ymc_final(struct ymd_parser *p) {
	{
		struct znode *i = p->lnk, *last = i;
		while (i) {
			last = i;
			i = i->chain;
			vm_free(p->vm, last);
		}
		p->lnk = NULL;
	}
	{
		struct func_env *i = p->env, *last = i;
		while (i) {
			last = i;
			i = i->chain;
			if (!last->core->ref) {
				blk_shrink(p->vm, last->core);
				blk_final(p->vm, last->core);
				mm_free(p->vm, last->core, 1, sizeof(*(last->core)));
				ymd_pop(ioslate(p->vm), 1);
			}
			hmap_final(p->vm, &last->kval);
			vm_free(p->vm, last);
		}
		p->env = NULL;
	}
}

int ymc_compile(struct ymd_parser *p, struct chunk *blk) {
	lex_next(&p->lex, &p->lah);
	ymk_chunk_reserved(p, blk);
	ymk_enter(p, blk);
	if (setjmp(p->jpt)) {
		ymc_final(p);
		return -1;
	}
	while (ymc_peek(p) != EOS)
		parse_stmt(p);
	ymk_emit_end(p);
	ymk_leave(p);
	assert(!p->env);
	ymc_final(p);
	return 0;
}

int ymd_compile(struct ymd_context *l, const char *name,
                const char *file, const char *code) {
	struct ymd_parser p;
	struct chunk *blk;
	int rv;
	memset(&p, 0, sizeof(p));
	p.vm = l->vm;
	lex_init(&p.lex, file, code);
	blk = ymk_chunk(&p);
	ymd_func(l, blk, name);
	if ((rv = ymc_compile(&p, blk)) < 0)
		ymd_pop(l, 1);
	return rv;
}

int ymd_compilef(struct ymd_context *l, const char *name,
                 const char *file, FILE *fp) {
	long len;
	int rv;
	char *code = NULL;
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	rewind(fp);
	code = vm_zalloc(l->vm, len + 1);
	rv = fread(code, 1, len, fp);
	if (rv <= 0)
		goto fail;
	rv = ymd_compile(l, name, file, code);
fail:
	if (code) vm_free(l->vm, code);
	return rv;
}
