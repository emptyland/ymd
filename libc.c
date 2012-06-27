#include "3rd/regex/regex.h"
#include "state.h"
#include "value.h"
#include "memory.h"
#include "compiler.h"
#include "encode.h"
#include "libc.h"
#include <stdio.h>
#include <setjmp.h>

struct posix_regex {
	regex_t core; // NOTE: This field must be first!
	int sub;
};
static const char *T_REGEX = "regex";

static const char *T_STRBUF = "strbuf";

struct ansic_file {
	struct yio_stream op;
	FILE *fp;
};
static const char *T_STREAM = "stream";

#define PRINT_SPLIT " "

// Format context
#define FMTX_STATIC_MAX 260
struct fmtx {
	char kbuf[FMTX_STATIC_MAX];
	char *dy;
	int last;
	int max;
};
#define FMTX_INIT { {0}, NULL, 0, FMTX_STATIC_MAX }

static inline void fmtx_final(struct fmtx *self) {
	if (self->dy) vm_free(self->dy);
	memset(self->kbuf, 0, sizeof(self->kbuf));
	self->dy = NULL;
	self->last = 0;
	self->max  = FMTX_STATIC_MAX;
}

static inline char *fmtx_buf(struct fmtx *self) {
	return !self->dy ? self->kbuf : self->dy;
}

static inline int fmtx_remain(struct fmtx *self) {
	return self->max - self->last;
}

static inline char *fmtx_last(struct fmtx *self) {
	return fmtx_buf(self) + self->last;
}

static inline char *fmtx_add(struct fmtx *self) {
	self->last += strlen(fmtx_last(self));
	return fmtx_buf(self);
}

static void fmtx_need(struct fmtx *self, int n) {
	if (self->last + n <= (int)sizeof(self->kbuf))
		return;
	if (self->last + n <= self->max)
		return;
	self->max = (self->last + n) * 3 / 2 + FMTX_STATIC_MAX;
	if (!self->dy) {
		self->dy = vm_zalloc(self->max);
		memcpy(self->dy, self->kbuf, self->last);
	} else {
		self->dy = vm_realloc(self->dy, self->max);
	}
}

static const char *fmtx_append(struct fmtx *self, const char *src, int n) {
	fmtx_need(self, n);
	memcpy(fmtx_last(self), src, n);
	self->last += n;
	return fmtx_buf(self);
}

static const char *tostring(struct fmtx *ctx, const struct variable *var) {
	switch (var->type) {
	case T_NIL:
		return fmtx_append(ctx, "nil", 3);
	case T_INT:
		fmtx_need(ctx, 24);
		snprintf(fmtx_last(ctx), fmtx_remain(ctx), "%lld", var->value.i);
		return fmtx_add(ctx);
	case T_BOOL:
		return var->value.i ? fmtx_append(ctx, "true", 4)
			: fmtx_append(ctx, "false", 5);
	case T_EXT:
		fmtx_need(ctx, 24);
		snprintf(fmtx_last(ctx), fmtx_remain(ctx), "@%p", var->value.ext);
		return fmtx_add(ctx);
	case T_KSTR:
		return fmtx_append(ctx, kstr_k(var)->land, kstr_k(var)->len);
	case T_FUNC:
		return fmtx_append(ctx, func_k(var)->proto->land,
		                   func_k(var)->proto->len);
	case T_DYAY: {
		int i;
		fmtx_append(ctx, "[", 1);
		for (i = 0; i < dyay_k(var)->count; ++i) {
			if (i > 0) fmtx_append(ctx, ", ", 2);
			tostring(ctx, dyay_k(var)->elem + i);
		}
		} return fmtx_append(ctx, "]", 1);
	case T_HMAP: {
		struct kvi *initial = hmap_k(var)->item,
				   *i = NULL,
				   *k = initial + (1 << hmap_k(var)->shift);
		int f = 0;
		fmtx_append(ctx, "{", 1);
		for (i = initial; i != k; ++i) {
			if (!i->flag) continue;
			if (f++ > 0) fmtx_append(ctx, ", ", 2);
			tostring(ctx, &i->k);
			fmtx_append(ctx, " : ", 3);
			tostring(ctx, &i->v);
		}
		} return fmtx_append(ctx, "}", 1);
	case T_SKLS: {
		struct sknd *initial = skls_k(var)->head->fwd[0],
				    *i = NULL;
		int f = 0;
		fmtx_append(ctx, "@{", 2);
		for (i = initial; i != NULL; i = i->fwd[0]) {
			if (f++ > 0) fmtx_append(ctx, ", ", 2);
			tostring(ctx, &i->k);
			fmtx_append(ctx, " : ", 3);
			tostring(ctx, &i->v);
		}
		} return fmtx_append(ctx, "}", 1);
	case T_MAND: {
		fmtx_append(ctx, "(", 1);
		if (mand_k(var)->tt)
			fmtx_append(ctx, mand_k(var)->tt, strlen(mand_k(var)->tt));
		else
			fmtx_append(ctx, "*", 1);
		fmtx_append(ctx, ")", 1);
		fmtx_need(ctx, 10 + 24 + 2);
		snprintf(fmtx_last(ctx), fmtx_remain(ctx), "[%d@%p]",
		         mand_k(var)->len, mand_k(var)->land);
		} return fmtx_add(ctx);
	default:
		assert(0);
		break;
	}
	return NULL;
}

static int libx_print(struct context *l) {
	int i;
	struct fmtx fx = FMTX_INIT;
	struct dyay *argv = ymd_argv(l);
	if (!argv) { puts(""); return 0; }
	for (i = 0; i < argv->count; ++i) {
		const char *raw = tostring(&fx, argv->elem + i);
		if (i > 0) printf(PRINT_SPLIT);
		fwrite(raw, fx.last, 1, stdout);
		fmtx_final(&fx);
	}
	puts("");
	return 0;
}

static inline void dyay_range_chk(const struct dyay *arr, ymd_int_t i) {
	if (i < 0 || i >= arr->count)
		vm_die("array index out of range, %lld", i);
}

static void dyay_do_insert(struct dyay *self, struct context *l) {
	switch (ymd_argv_chk(l, 2)->count) {
	case 2:
		*dyay_add(self) = *ymd_argv_get(l, 1);
		break;
	case 3: {
		ymd_int_t i = int_of(ymd_argv_get(l, 1));
		dyay_range_chk(self, i);
		*dyay_insert(self, i) = *ymd_argv_get(l, 2);
		} break;
	default:
		vm_die("Too many arguments, %d", ymd_argv(l)->count);
		break;
	}
}

static void checked_put(struct variable *arg0,
                        const struct variable *k,
						const struct variable *v) {
	if (is_nil(k) || is_nil(v))
		vm_die("Value can not be `nil` in k-v pair");
	switch (arg0->type) {
	case T_HMAP:
		*hmap_put(hmap_x(arg0), k) = *v;
		break;
	case T_SKLS:
		*skls_put(skls_x(arg0), k) = *v;
		break;
	default:
		vm_die("This type: `%s` is not be support", typeof_kz(arg0->type));
		break;
	}
}

static int libx_insert(struct context *l) {
	struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_DYAY:
		dyay_do_insert(dyay_x(arg0), l);
		break;
	default:
		checked_put(arg0, ymd_argv_get(l, 1), ymd_argv_get(l, 2));
		break;
	}
	return 0;
}

static int libx_append(struct context *l) {
	int i;
	struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_DYAY:
		for (i = 1; i < ymd_argv_chk(l, 2)->count; ++i)
			*dyay_add(dyay_x(arg0)) = ymd_argv(l)->elem[i];
		break;
	case T_HMAP:
		for (i = 1; i < ymd_argv_chk(l, 2)->count; ++i) {
			struct dyay *pair = dyay_of(ymd_argv_get(l, i));
			*hmap_get(hmap_x(arg0), pair->elem) = pair->elem[1];
		}
		break;
	case T_SKLS:
		for (i = 1; i < ymd_argv_chk(l, 2)->count; ++i) {
			struct dyay *pair = dyay_of(ymd_argv_get(l, i));
			*skls_get(skls_x(arg0), pair->elem) = pair->elem[1];
		}
		break;
	default:
		vm_die("This type: `%s` is not be support", typeof_kz(arg0->type));
		break;
	}
	return 0;
}

static int libx_len(struct context *l) {
	const struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_NIL:
		ymd_push_int(l, 0);
		break;
	case T_KSTR:
		ymd_push_int(l, kstr_k(arg0)->len);
		break;
	case T_DYAY:
		ymd_push_int(l, dyay_k(arg0)->count);
		break;
	case T_HMAP: {
		ymd_int_t n = 0;
		struct kvi *i = hmap_k(arg0)->item,
				   *k = i + (1 << hmap_k(arg0)->shift);
		for (; i != k; ++i)
			if (i->flag) ++n;
		ymd_push_int(l, n);
		} break;
	case T_SKLS: {
		ymd_int_t n = 0;
		struct sknd *i = skls_k(arg0)->head->fwd[0];
		for (; i != NULL; i = i->fwd[0]) ++n;
		ymd_push_int(l, n);
		} break;
	default:
		ymd_push_nil(l);
		vm_die("This type: `%s` is not be support, "
		       "need a container or string type",
		       typeof_kz(arg0->type));
		return 1;
	}
	return 1;
}

// int 12   -> "12"
// lite 12  -> "@0x000000000000000C"
// nil      -> "nil"
// bool     -> "true" or "false"
// string   -> "Hello, World"
// array    -> "{1,2,name,{1,2}}"
// function -> "func [...] (...) {...}"
// hashmap  -> "{name:John, content:{1,2,3}}"
// skiplist -> "@{name:John, content:{1,2,3}}"
// managed  -> "(stream)[24@0x08067FF]"
static int libx_str(struct context *l) {
	const struct variable *arg0 = ymd_argv_get(l, 0);
	struct fmtx fx = FMTX_INIT;
	const char *z = tostring(&fx, arg0);
	struct kstr *rv = ymd_kstr(z, fx.last);
	vset_kstr(ymd_push(l), rv);
	fmtx_final(&fx);
	return 1;
}

//------------------------------------------------------------------------------
// Foreach closures
//------------------------------------------------------------------------------
static int libx_end(struct context *l) {
	vset_nil(ymd_push(l));
	return 1;
}

static int step_iter(struct context *l) {
	struct variable *i = ymd_bval(l, 0),
					*m = ymd_bval(l, 1),
					*s = ymd_bval(l, 2),
					rv;
	rv.type = T_NIL;
	rv.value.i = 0;
	if (s->value.i > 0 && i->value.i <= m->value.i) {
		rv.type = T_INT;
		rv.value.i = i->value.i;
	} else if (s->value.i < 0 && i->value.i >= m->value.i) {
		rv.type = T_INT;
		rv.value.i = i->value.i;
	}
	i->value.i += s->value.i;
	*ymd_push(l) = rv;
	return 1;
}

static struct func *new_step_iter(struct context *l,
                                  ymd_int_t i, ymd_int_t m, ymd_int_t s) {
	struct func *iter;
	(void)l;
	if ((s == 0) || (i > m && s > 0) || (i < m && s < 0))
		return func_new_c(libx_end, "end");
	iter = func_new_c(step_iter, "__step_iter__");
	iter->n_bind = 3;
	vset_int(func_bval(iter, 0), i);
	vset_int(func_bval(iter, 1), m);
	vset_int(func_bval(iter, 2), s);
	return iter;
}

#define ITER_KEY   0
#define ITER_VALUE 1
#define ITER_KV    2

// Dynamic array iterator
// bind[0]: struct variable *i; current pointer
// bind[1]: struct variable *m; end of pointer
// bind[2]: int idx; index counter
// bind[3]: iterator flag
static int dyay_iter(struct context *l) {
	struct variable *i = ymd_bval(l, 0)->value.ext,
	                *m = ymd_bval(l, 1)->value.ext;
	if (i >= m) {
		vset_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(ymd_bval(l, 3))) {
	case ITER_KEY: {
		ymd_int_t idx = int_of(ymd_bval(l, 2));
		vset_int(ymd_push(l), idx);
		vset_int(ymd_bval(l, 2), idx + 1);
		} break;
	case ITER_VALUE: {
		*ymd_push(l) = *i;
		} break;
	case ITER_KV: {
		struct dyay *rv = dyay_new(0);
		ymd_int_t idx = int_of(ymd_bval(l, 2));
		vset_int(dyay_add(rv), idx);
		*dyay_add(rv) = *i;
		vset_dyay(ymd_push(l), rv);
		vset_int(ymd_bval(l, 2), idx + 1);
		} break;
	default:
		assert(0);
		break;
	}
	vset_ext(ymd_bval(l, 0), i + 1);
	return 1;
}

static inline struct kvi *move2valid(struct kvi *i, struct kvi *m) {
	while (i->flag == 0 && i < m) ++i;
	return i;
}

// Hash map iterator
// bind[0]: struct kvi *i; current pointer
// bind[1]: struct kvi *m; end of pointer
// bind[2]: iterator flag
static int hmap_iter(struct context *l) {
	struct kvi *i = ymd_bval(l, 0)->value.ext,
			   *m = ymd_bval(l, 1)->value.ext;
	if (i >= m) {
		vset_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(ymd_bval(l, 2))) {
	case ITER_KEY:
		*ymd_push(l) = i->k;
		break;
	case ITER_VALUE:
		*ymd_push(l) = i->v;
		break;
	case ITER_KV: {
		struct dyay *rv = dyay_new(0);
		*dyay_add(rv) = i->k;
		*dyay_add(rv) = i->v;
		vset_dyay(ymd_push(l), rv);
		} break;
	default:
		assert(0);
		break;
	}
	vset_ext(ymd_bval(l, 0), move2valid(i + 1, m));
	return 1;
}

// Skip list iterator
// bind[0]: struct sknd *i; current pointer
// bind[1]: iterator flag
static int skls_iter(struct context *l) {
	struct sknd *i = ymd_bval(l, 0)->value.ext;
	if (!i) {
		vset_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(ymd_bval(l, 1))) {
	case ITER_KEY:
		*ymd_push(l) = i->k;
		break;
	case ITER_VALUE:
		*ymd_push(l) = i->v;
		break;
	case ITER_KV: {
		struct dyay *rv = dyay_new(0);
		*dyay_add(rv) = i->k;
		*dyay_add(rv) = i->v;
		vset_dyay(ymd_push(l), rv);
		} break;
	default:
		assert(0);
		break;
	}
	vset_ext(ymd_bval(l, 0), i->fwd[0]);
	return 1;
}

static struct func *new_contain_iter(
	struct context *l,
	const struct variable *obj,
	int flag) {
	struct func *iter;
	(void)l;
	switch (obj->type) {
	case T_DYAY:
		iter = func_new_c(dyay_iter, "__dyay_iter__");
		iter->n_bind = 4;
		vset_ext(func_bval(iter, 0), dyay_k(obj)->elem);
		vset_ext(func_bval(iter, 1), dyay_k(obj)->elem + dyay_k(obj)->count);
		vset_int(func_bval(iter, 2), 0);
		vset_int(func_bval(iter, 3), flag);
		break;
	case T_HMAP: {
		struct kvi *m = hmap_k(obj)->item + (1 << hmap_k(obj)->shift),
				   *i = move2valid(hmap_k(obj)->item, m);
		if (i >= m)
			return func_new_c(libx_end, "end"); // FIXME:
		iter = func_new_c(hmap_iter, "__hmap_iter__");
		iter->n_bind = 3;
		vset_ext(func_bval(iter, 0), i);
		vset_ext(func_bval(iter, 1), m);
		vset_int(func_bval(iter, 2), flag);
		} break;
	case T_SKLS: {
		struct sknd *i = skls_k(obj)->head->fwd[0];
		if (!i)
			return func_new_c(libx_end, "end"); // FIXME:
		iter = func_new_c(skls_iter, "__skls_iter__");
		iter->n_bind = 2;
		vset_ext(func_bval(iter, 0), i);
		vset_int(func_bval(iter, 1), flag);
		} break;
	default:
		vm_die("Type is not be supported");
		break;
	}
	return iter;
}

// range(1,100) = {1,2,3,...100}
// range(1,100,2) = {1,3,5,...100}
// range({9,8,7}) = {9,8,7}
static int libx_range(struct context *l) {
	struct func *iter;
	const struct dyay *argv = ymd_argv_chk(l, 1);
	switch (argv->count) {
	case 1:
		iter = new_contain_iter(l, argv->elem, ITER_VALUE);
		break;
	case 2: {
		ymd_int_t i = int_of(argv->elem),
				  m = int_of(argv->elem + 1);
		iter = new_step_iter(l, i, m, i > m ? -1 : +1);
		} break;
	case 3:
		iter = new_step_iter(l, int_of(argv->elem),
		                     int_of(argv->elem + 1),
							 int_of(argv->elem + 2));
		break;
	default:
		vm_die("Bad arguments, need 1 to 3");
		break;
	}
	ymd_push_func(l, iter);
	return 1;
}

static int libx_rank(struct context *l) {
	struct func *iter = new_contain_iter(l, ymd_argv_get(l, 0), ITER_KV);
	vset_func(ymd_push(l), iter);
	return 1;
}

static int libx_ranki(struct context *l) {
	struct func *iter = new_contain_iter(l, ymd_argv_get(l, 0), ITER_KEY);
	vset_func(ymd_push(l), iter);
	return 1;
}

static int libx_panic(struct context *l) {
	int i;
	struct dyay *argv = ymd_argv(l);
	struct fmtx fx = FMTX_INIT;
	if (!argv || argv->count == 0)
		vm_die("Unknown");
	for (i = 0; i < argv->count; ++i) {
		if (i > 0) fmtx_append(&fx, " ", 1);
		tostring(&fx, argv->elem + i);
	}
	vm_die(fmtx_buf(&fx));
	fmtx_final(&fx);
	return 0;
}

//------------------------------------------------------------------------------
// String Buffer: strbuf
//------------------------------------------------------------------------------
static inline struct fmtx *strbuf_of(struct variable *var) {
	struct mand *pm = mand_of(var);
	if (pm->tt != T_STRBUF)
		vm_die("Builtin fatal:%s:%d", __FILE__, __LINE__);
	return (struct fmtx *)pm->land;
}

static int strbuf_final(struct fmtx *sb) {
	fmtx_final(sb);
	return 0;
}

static int libx_strbuf(struct context *l) {
	struct mand *x = mand_new(NULL, sizeof(struct fmtx),
	                          (ymd_final_t)strbuf_final);
	x->tt = T_STRBUF;
	((struct fmtx *)x->land)->max = FMTX_STATIC_MAX;
	vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_strfin(struct context *l) {
	struct fmtx *self;
	struct kstr *rv;
	self = strbuf_of(ymd_argv_get(l, 0));
	if (self->last == 0) {
		vset_nil(ymd_push(l));
		goto done;
	}
	rv = ymd_kstr(fmtx_buf(self), self->last);
	vset_kstr(ymd_push(l), rv);
done:
	strbuf_final(self);
	return 1;
}

static int libx_strcat(struct context *l) {
	int i;
	struct dyay *argv = ymd_argv_chk(l, 2);
	struct fmtx *self = strbuf_of(argv->elem);
	for (i = 1; i < argv->count; ++i)
		tostring(self, argv->elem + i);
	return 0;
}

//------------------------------------------------------------------------------
// File
//------------------------------------------------------------------------------
static inline struct ansic_file *ansic_file_of(struct variable *var) {
	struct mand *pm = mand_of(var);
	if (pm->tt != T_STREAM)
		vm_die("Builtin fatal:%s:%d", __FILE__, __LINE__);
	return (struct ansic_file *)pm->land;
}

static inline struct yio_stream *yio_stream_of(struct variable *var) {
	struct mand *pm = mand_of(var);
	if (pm->tt != T_STREAM)
		vm_die("Builtin fatal:%s:%d", __FILE__, __LINE__);
	return (struct yio_stream *)pm->land;
}

static struct kstr *ansic_file_readn(struct ansic_file *self, ymd_int_t n) {
	struct kstr *rv = NULL;
	char *buf = vm_zalloc(n);
	int rvl = fread(buf, 1, n, self->fp);
	if (rvl <= 0)
		goto done;
	rv = kstr_new(buf, rvl);
done:
	vm_free(buf);
	return rv;
}

static struct kstr *ansic_file_readall(struct ansic_file *self) {
	ymd_int_t len;
	fseek(self->fp, 0, SEEK_END);
	len = ftell(self->fp);
	rewind(self->fp);
	return ansic_file_readn(self, len);
}

static struct kstr *ansic_file_readline(struct ansic_file *self) {
	char line[1024];
	char *rv = fgets(line, sizeof(line), self->fp);
	if (!rv)
		return NULL;
	return kstr_new(line, -1);
}

static int ansic_file_read(struct context *l) {
	struct kstr *rv = NULL;
	struct variable *arg1 = NULL;
	struct ansic_file *self = ansic_file_of(ymd_argv_get(l, 0));
	if (ymd_argv_chk(l, 1)->count == 1) {
		rv = ansic_file_readn(self, 128);
		goto done;
	}
	arg1 = ymd_argv_get(l, 1);
	switch (arg1->type) {
	case T_KSTR:
		if (strcmp(kstr_k(arg1)->land, "*all") == 0)
			rv = ansic_file_readall(self);
		else if (strcmp(kstr_k(arg1)->land, "*line") == 0)
			rv = ansic_file_readline(self);
		else
			vm_die("Bad read() option: %s", kstr_k(arg1)->land);
		break;
	case T_INT: {
		ymd_int_t n = int_of(ymd_argv_get(l, 1));
		rv = ansic_file_readn(self, n);
		} break;
	default:
		vm_die("Bad read() option");
		break;
	}
done:
	if (!rv)
		vset_nil(ymd_push(l));
	else
		vset_kstr(ymd_push(l), rv);
	return 1;
}

static int ansic_file_write(struct context *l) {
	struct ansic_file *self = ansic_file_of(ymd_argv_get(l, 0));
	struct kstr *bin = NULL;
	if (is_nil(ymd_argv_get(l, 1)))
		return 0;
	bin = kstr_of(ymd_argv_get(l, 1));
	int rv = fwrite(bin->land, 1, bin->len, self->fp);
	if (rv < 0)
		vm_die("Write file failed!");
	return 0;
}

static int ansic_file_final(struct ansic_file *self) {
	if (self->fp) {
		fclose(self->fp);
		self->fp = NULL;
	}
	return 0;
}

static int libx_open(struct context *l) {
	const char *mod = "r";
	struct mand *x = mand_new(NULL, sizeof(struct ansic_file),
	                          (ymd_final_t)ansic_file_final);
	struct ansic_file *rv = (struct ansic_file *)(x->land);
	x->tt = T_STREAM;
	rv->op.read = ansic_file_read;
	rv->op.write = ansic_file_write;
	if (ymd_argv_chk(l, 1)->count > 1)
		mod = kstr_of(ymd_argv_get(l, 1))->land;
	rv->fp = fopen(kstr_of(ymd_argv_get(l, 0))->land, mod);
	if (!rv->fp)
		vset_nil(ymd_push(l));
	else
		vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_read(struct context *l) {
	struct yio_stream *s = yio_stream_of(ymd_argv_get(l, 0));
	return s->read(l);
}

static int libx_write(struct context *l) {
	struct yio_stream *s = yio_stream_of(ymd_argv_get(l, 0));
	return s->write(l);
}

static int libx_close(struct context *l) {
	struct mand *pm = mand_of(ymd_argv_get(l, 0));
	struct yio_stream *self = yio_stream_of(ymd_argv_get(l, 0));
	assert(pm->final);
	return (*pm->final)(self);

}

//------------------------------------------------------------------------------
// Regex pattern and match
//------------------------------------------------------------------------------
static inline struct posix_regex *posix_regex_of(struct variable *var) {
	struct mand *pm = mand_of(var);
	if (pm->tt != T_REGEX)
		vm_die("Builtin fatal:%s:%d", __FILE__, __LINE__);
	return (struct posix_regex *)pm->land;
}

static int posix_regex_final(struct posix_regex *self) {
	regfree(&self->core);
	return 0;
}

// Example:
// regex = pattern("^[0-9]+$")
// result = match(regex, "0000")
// print(result)
static int libx_pattern(struct context *l) {
	int err = 0, cflag = REG_EXTENDED;
	struct kstr *arg0 = kstr_of(ymd_argv_get(l, 0));
	struct mand *x = mand_new(NULL, sizeof(*x),
	                          (ymd_final_t)posix_regex_final);
	struct posix_regex *self = (struct posix_regex *)x->land;
	x->tt = T_REGEX;
	switch (ymd_argv(l)->count) {
	case 1:
		cflag = REG_NOSUB;
		break;
	case 2: {
		struct kstr *arg1 = kstr_of(ymd_argv_get(l, 1));
		if (strcmp(arg1->land, "*nosub") == 0)
			cflag |= REG_NOSUB;
		else if (strcmp(arg1->land, "*sub") == 0)
			self->sub = 1;
		else
			vm_die("Bad regex option: %s", arg1->land);
		} break;
	default:
		vm_die("Too many args, %d", ymd_argv(l)->count);
		break;
	}
	err = regcomp(&self->core, arg0->land, cflag);
	if (err)
		vset_nil(ymd_push(l));
	else
		vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_match(struct context *l) {
	struct posix_regex *self = posix_regex_of(ymd_argv_get(l, 0));
	struct kstr *arg1 = kstr_of(ymd_argv_get(l, 1));
	int err = 0;
	if (self->sub) {
		struct dyay *rv = dyay_new(0);
		regmatch_t matched[128];
		err = regexec(&self->core, arg1->land,
		              sizeof(matched)/sizeof(matched[0]),
					  matched, 0);
		if (!err) {
			regmatch_t *i;
			for (i = matched; i->rm_so != -1; ++i) {
				struct kstr *sub = ymd_kstr(arg1->land + i->rm_so,
				                            i->rm_eo - i->rm_so);
				vset_kstr(dyay_add(rv), sub);
			}
		}
		vset_dyay(ymd_push(l), rv);
	} else {
		err = regexec(&self->core, arg1->land, 0, NULL, 0);
		vset_bool(ymd_push(l), !err);
	}
	return 1;
}

//------------------------------------------------------------------------------
// Library:
//------------------------------------------------------------------------------
#define MAX_BLOCK_NAME_LEN 260 * 2

static const char *file2blknam(const char *name, char *buf, int len) {
	int i = 0;
	char tran[MAX_BLOCK_NAME_LEN];
	memset(tran, 0, sizeof(tran));
	while (*name) {
		char ch = *name++;
		if (ch == '.') { // Transform `.` to `_2d_`
			strcat(tran + i, "_2d_");
			i += 4;
		} else {
			tran[i] = ch;
			++i;
		}
	}
	tran[i] = 0; // finish
	snprintf(buf, len, "__blk_%s__", tran);
	return buf;
}

static int libx_import(struct context *l) {
	int i;
	char blknam[MAX_BLOCK_NAME_LEN];
	struct func *block;
	struct kstr *name = kstr_of(ymd_argv_get(l, 0));
	FILE *fp = fopen(name->land, "r");
	if (!fp)
		vm_die("Can not open import file: %s", name->land);
	block = func_compilef(
		file2blknam(name->land, blknam, sizeof(blknam)), name->land, fp);
	fclose(fp);
	if (!block)
		vm_die("Import fatal, syntax error in file: `%s`", name->land);
	vset_func(ymd_push(l), block);
	for (i = 1; i < ymd_argv(l)->count; ++i)
		*ymd_push(l) = *ymd_argv_get(l, i);
	return func_call(block, ymd_argv(l)->count - 1, 0);
}

static int libx_eval(struct context *l) {
	int i;
	struct func *chunk;
	struct kstr *script = kstr_of(ymd_argv_get(l, 0));
	chunk = func_compile("__blk_eval__", NULL, script->land);
	if (!chunk) {
		vset_nil(ymd_push(l));
		return 1;
	}
	vset_func(ymd_push(l), chunk);
	for (i = 1; i < ymd_argv(l)->count; ++i)
		*ymd_push(l) = *ymd_argv_get(l, i);
	return func_call(chunk, ymd_argv(l)->count - 1, 0);
}

static int libx_compile(struct context *l) {
	struct func *chunk;
	struct kstr *script = kstr_of(ymd_argv_get(l, 0));
	chunk = func_compile("__blk_compile__", NULL, script->land);
	if (!chunk) {
		vset_nil(ymd_push(l));
		return 1;
	}
	vset_func(ymd_push(l), chunk);
	return 1;
}

static int libx_env(struct context *l) {
	struct kstr *which = kstr_of(ymd_argv_get(l, 0));
	if (strcmp(which->land, "*global") == 0)
		vset_hmap(ymd_push(l), vm()->global);
	else if (strcmp(which->land, "*kpool") == 0)
		vset_hmap(ymd_push(l), vm()->kpool);
	else if (strcmp(which->land, "*current") == 0)
		vset_func(ymd_push(l), l->info->chain->run);
	else
		vset_nil(ymd_push(l));
	return 1;
}

static int libx_atoi(struct context *l) {
	struct kstr *arg0 = kstr_of(ymd_argv_get(l, 0));
	int ok = 1;
	ymd_int_t i = dtoll(arg0->land, &ok);
	if (ok)
		vset_int(ymd_push(l), i);
	else
		vset_nil(ymd_push(l));
	return 1;
}

static int libx_exit(struct context *l) {
	(void)l;
	longjmp(l->jpt, 2);
}

LIBC_BEGIN(Builtin)
	LIBC_ENTRY(print)
	LIBC_ENTRY(insert)
	LIBC_ENTRY(append)
	LIBC_ENTRY(len)
	LIBC_ENTRY(range)
	LIBC_ENTRY(rank)
	LIBC_ENTRY(ranki)
	LIBC_ENTRY(str)
	LIBC_ENTRY(atoi)
	LIBC_ENTRY(panic)
	LIBC_ENTRY(strbuf)
	LIBC_ENTRY(strcat)
	LIBC_ENTRY(strfin)
	LIBC_ENTRY(open)
	LIBC_ENTRY(read)
	LIBC_ENTRY(write)
	LIBC_ENTRY(close)
	LIBC_ENTRY(pattern)
	LIBC_ENTRY(match)
	LIBC_ENTRY(import)
	LIBC_ENTRY(eval)
	LIBC_ENTRY(compile)
	LIBC_ENTRY(env)
	LIBC_ENTRY(exit)
LIBC_END


int ymd_load_lib(ymd_libc_t lbx) {
	const struct libfn_entry *i;
	for (i = lbx; i->native != NULL; ++i) {
		struct kstr *kz = ymd_kstr(i->symbol.z, i->symbol.len);
		struct func *fn = func_new_c(i->native, kz->land);
		struct variable key;
		key.type = T_KSTR;
		key.value.ref = gcx(kz);
		vset_func(hmap_put(vm()->global, &key), fn);
	}
	return 0;
}
