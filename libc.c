#include "3rd/regex/regex.h"
#include "tostring.h"
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

static int libx_print(struct ymd_context *l) {
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

static void do_insert(struct ymd_context *l, struct dyay *self) {
	struct variable *elem;
	switch (ymd_argv_chk(l, 2)->count) {
	case 2:
		elem = ymd_argv_get(l, 1);
		if (is_nil(elem))
			vm_die(l->vm, "Element of array can not be `nil`");
		*dyay_add(l->vm, self) = *elem;
		break;
	case 3: {
		ymd_int_t i = int_of(l->vm, ymd_argv_get(l, 1));
		if (i < 0 || i >= self->count)
			vm_die(l->vm, "array index out of range, %lld", i);
		elem = ymd_argv_get(l, 2);
		if (is_nil(elem))
			vm_die(l->vm, "Element in array can not be `nil`");
		*dyay_insert(l->vm, self, i) = *elem;
		} break;
	default:
		vm_die(l->vm, "Too many arguments, %d", ymd_argv(l)->count);
		break;
	}
}

static void checked_put(struct ymd_context *l,
                        struct variable *arg0,
                        const struct variable *k,
						const struct variable *v) {
	if (is_nil(k) || is_nil(v))
		vm_die(l->vm, "Value can not be `nil` in k-v pair");
	switch (arg0->type) {
	case T_HMAP:
		*hmap_put(l->vm, hmap_x(arg0), k) = *v;
		break;
	case T_SKLS:
		*skls_put(l->vm, skls_x(arg0), k) = *v;
		break;
	default:
		vm_die(l->vm, "This type: `%s` is not be support", typeof_kz(arg0->type));
		break;
	}
}

// insert to array:
//     insert(array, elem)
//     insert(array, position, elem)
// insert to map:
//     insert(map, key, value)
//
static int libx_insert(struct ymd_context *l) {
	struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_DYAY:
		do_insert(l, dyay_x(arg0));
		break;
	default:
		checked_put(l, arg0, ymd_argv_get(l, 1), ymd_argv_get(l, 2));
		break;
	}
	return 0;
}

static int libx_append(struct ymd_context *l) {
	int i;
	struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_DYAY:
		for (i = 1; i < ymd_argv_chk(l, 2)->count; ++i)
			*dyay_add(l->vm, dyay_x(arg0)) = ymd_argv(l)->elem[i];
		break;
	case T_HMAP:
		for (i = 1; i < ymd_argv_chk(l, 2)->count; ++i) {
			struct dyay *pair = dyay_of(l->vm, ymd_argv_get(l, i));
			*hmap_get(hmap_x(arg0), pair->elem) = pair->elem[1];
		}
		break;
	case T_SKLS:
		for (i = 1; i < ymd_argv_chk(l, 2)->count; ++i) {
			struct dyay *pair = dyay_of(l->vm, ymd_argv_get(l, i));
			*skls_get(skls_x(arg0), pair->elem) = pair->elem[1];
		}
		break;
	default:
		vm_die(l->vm, "This type: `%s` is not be support", typeof_kz(arg0->type));
		break;
	}
	return 0;
}

// len(arg)
// argument follow:
// nil      : always be 0;
// string   : length;
// array    : number of elements; 
// hashmap  : number of k-v pairs;
// skiplist : number of k-v pairs;
static int libx_len(struct ymd_context *l) {
	const struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_NIL:
		vset_int(ymd_push(l), 0);
		break;
	case T_KSTR:
		vset_int(ymd_push(l), kstr_k(arg0)->len);
		break;
	case T_DYAY:
		vset_int(ymd_push(l), dyay_k(arg0)->count);
		break;
	case T_HMAP: {
		ymd_int_t n = 0;
		struct kvi *i = hmap_k(arg0)->item,
				   *k = i + (1 << hmap_k(arg0)->shift);
		for (; i != k; ++i)
			if (i->flag) ++n;
		vset_int(ymd_push(l), n);
		} break;
	case T_SKLS: {
		ymd_int_t n = 0;
		struct sknd *i = skls_k(arg0)->head->fwd[0];
		for (; i != NULL; i = i->fwd[0]) ++n;
		vset_int(ymd_push(l), n);
		} break;
	default:
		vset_nil(ymd_push(l));
		vm_die(l->vm, "This type: `%s` is not be support, "
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
static int libx_str(struct ymd_context *l) {
	const struct variable *arg0 = ymd_argv_get(l, 0);
	struct fmtx fx = FMTX_INIT;
	const char *z = tostring(&fx, arg0);
	struct kstr *rv = ymd_kstr(l->vm, z, fx.last);
	vset_kstr(ymd_push(l), rv);
	fmtx_final(&fx);
	return 1;
}

//------------------------------------------------------------------------------
// Foreach closures
//------------------------------------------------------------------------------
static int libx_end(struct ymd_context *l) {
	vset_nil(ymd_push(l));
	return 1;
}

static int step_iter(struct ymd_context *l) {
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

static struct func *new_step_iter(struct ymd_context *l,
                                  ymd_int_t i, ymd_int_t m, ymd_int_t s) {
	struct func *iter;
	(void)l;
	if ((s == 0) || (i > m && s > 0) || (i < m && s < 0))
		return func_new_c(l->vm, libx_end, "end");
	iter = func_new_c(l->vm, step_iter, "__step_iter__");
	iter->n_bind = 3;
	vset_int(func_bval(l->vm, iter, 0), i);
	vset_int(func_bval(l->vm, iter, 1), m);
	vset_int(func_bval(l->vm, iter, 2), s);
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
static int dyay_iter(struct ymd_context *l) {
	struct variable *i = ymd_bval(l, 0)->value.ext,
	                *m = ymd_bval(l, 1)->value.ext;
	if (i >= m) {
		vset_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(l->vm, ymd_bval(l, 3))) {
	case ITER_KEY: {
		ymd_int_t idx = int_of(l->vm, ymd_bval(l, 2));
		vset_int(ymd_push(l), idx);
		vset_int(ymd_bval(l, 2), idx + 1);
		} break;
	case ITER_VALUE: {
		*ymd_push(l) = *i;
		} break;
	case ITER_KV: {
		struct dyay *rv = dyay_new(l->vm, 0);
		ymd_int_t idx = int_of(l->vm, ymd_bval(l, 2));
		vset_int(dyay_add(l->vm, rv), idx);
		*dyay_add(l->vm, rv) = *i;
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

// Move hash map's slot to next valid one:
static inline struct kvi *move2valid(struct kvi *i, struct kvi *m) {
	while (i->flag == 0 && i < m) ++i;
	return i;
}

// Hash map iterator
// bind[0]: struct kvi *i; current pointer
// bind[1]: struct kvi *m; end of pointer
// bind[2]: iterator flag
static int hmap_iter(struct ymd_context *l) {
	struct kvi *i = ymd_bval(l, 0)->value.ext,
			   *m = ymd_bval(l, 1)->value.ext;
	if (i >= m) {
		vset_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(l->vm, ymd_bval(l, 2))) {
	case ITER_KEY:
		*ymd_push(l) = i->k;
		break;
	case ITER_VALUE:
		*ymd_push(l) = i->v;
		break;
	case ITER_KV: {
		struct dyay *rv = dyay_new(l->vm, 0);
		*dyay_add(l->vm, rv) = i->k;
		*dyay_add(l->vm, rv) = i->v;
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
static int skls_iter(struct ymd_context *l) {
	struct sknd *i = ymd_bval(l, 0)->value.ext;
	if (!i) {
		vset_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(l->vm, ymd_bval(l, 1))) {
	case ITER_KEY:
		*ymd_push(l) = i->k;
		break;
	case ITER_VALUE:
		*ymd_push(l) = i->v;
		break;
	case ITER_KV: {
		struct dyay *rv = dyay_new(l->vm, 0);
		*dyay_add(l->vm, rv) = i->k;
		*dyay_add(l->vm, rv) = i->v;
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
	struct ymd_context *l,
	const struct variable *obj,
	int flag) {
	struct func *iter;
	(void)l;
	switch (obj->type) {
	case T_DYAY:
		iter = func_new_c(l->vm, dyay_iter, "__dyay_iter__");
		iter->n_bind = 4;
		vset_ext(func_bval(l->vm, iter, 0), dyay_k(obj)->elem);
		vset_ext(func_bval(l->vm, iter, 1), dyay_k(obj)->elem + dyay_k(obj)->count);
		vset_int(func_bval(l->vm, iter, 2), 0);
		vset_int(func_bval(l->vm, iter, 3), flag);
		break;
	case T_HMAP: {
		struct kvi *m = hmap_k(obj)->item + (1 << hmap_k(obj)->shift),
				   *i = move2valid(hmap_k(obj)->item, m);
		if (i >= m)
			return func_new_c(l->vm, libx_end, "end"); // FIXME:
		iter = func_new_c(l->vm, hmap_iter, "__hmap_iter__");
		iter->n_bind = 3;
		vset_ext(func_bval(l->vm, iter, 0), i);
		vset_ext(func_bval(l->vm, iter, 1), m);
		vset_int(func_bval(l->vm, iter, 2), flag);
		} break;
	case T_SKLS: {
		struct sknd *i = skls_k(obj)->head->fwd[0];
		if (!i)
			return func_new_c(l->vm, libx_end, "end"); // FIXME:
		iter = func_new_c(l->vm, skls_iter, "__skls_iter__");
		iter->n_bind = 2;
		vset_ext(func_bval(l->vm, iter, 0), i);
		vset_int(func_bval(l->vm, iter, 1), flag);
		} break;
	default:
		vm_die(l->vm, "Type is not be supported");
		break;
	}
	return iter;
}

// range(1,100) = {1,2,3,...100}
// range(1,100,2) = {1,3,5,...100}
// range({9,8,7}) = {9,8,7}
static int libx_range(struct ymd_context *l) {
	struct func *iter;
	const struct dyay *argv = ymd_argv_chk(l, 1);
	switch (argv->count) {
	case 1:
		iter = new_contain_iter(l, argv->elem, ITER_VALUE);
		break;
	case 2: {
		ymd_int_t i = int_of(l->vm, argv->elem),
				  m = int_of(l->vm, argv->elem + 1);
		iter = new_step_iter(l, i, m, i > m ? -1 : +1);
		} break;
	case 3:
		iter = new_step_iter(l, int_of(l->vm, argv->elem),
		                     int_of(l->vm, argv->elem + 1),
							 int_of(l->vm, argv->elem + 2));
		break;
	default:
		vm_die(l->vm, "Bad arguments, need 1 to 3");
		break;
	}
	vset_func(ymd_push(l), iter);
	return 1;
}

static int libx_rank(struct ymd_context *l) {
	struct func *iter = new_contain_iter(l, ymd_argv_get(l, 0), ITER_KV);
	vset_func(ymd_push(l), iter);
	return 1;
}

static int libx_ranki(struct ymd_context *l) {
	struct func *iter = new_contain_iter(l, ymd_argv_get(l, 0), ITER_KEY);
	vset_func(ymd_push(l), iter);
	return 1;
}

static int libx_panic(struct ymd_context *l) {
	int i;
	struct dyay *argv = ymd_argv(l);
	struct fmtx fx = FMTX_INIT;
	if (!argv || argv->count == 0)
		vm_die(l->vm, "Unknown");
	for (i = 0; i < argv->count; ++i) {
		if (i > 0) fmtx_append(&fx, " ", 1);
		tostring(&fx, argv->elem + i);
	}
	vm_die(l->vm, fmtx_buf(&fx));
	fmtx_final(&fx);
	return 0;
}

//------------------------------------------------------------------------------
// String Buffer: strbuf
//------------------------------------------------------------------------------
static int strbuf_final(struct fmtx *sb) {
	fmtx_final(sb);
	return 0;
}

static int libx_strbuf(struct ymd_context *l) {
	struct mand *x = mand_new(l->vm, sizeof(struct fmtx),
	                          (ymd_final_t)strbuf_final);
	x->tt = T_STRBUF;
	((struct fmtx *)x->land)->max = FMTX_STATIC_MAX;
	vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_strfin(struct ymd_context *l) {
	struct fmtx *self = mand_land(l->vm, ymd_argv_get(l, 0),
	                              T_STRBUF);
	struct kstr *rv;
	if (self->last == 0) {
		vset_nil(ymd_push(l));
		goto done;
	}
	rv = ymd_kstr(l->vm, fmtx_buf(self), self->last);
	vset_kstr(ymd_push(l), rv);
done:
	strbuf_final(self);
	return 1;
}

static int libx_strcat(struct ymd_context *l) {
	int i;
	struct dyay *argv = ymd_argv_chk(l, 2);
	struct fmtx *self = mand_land(l->vm, argv->elem, T_STRBUF);
	for (i = 1; i < argv->count; ++i)
		tostring(self, argv->elem + i);
	return 0;
}

//------------------------------------------------------------------------------
// File
//------------------------------------------------------------------------------
static struct kstr *ansic_file_readn(struct ymd_mach *vm,
                                     struct ansic_file *self, ymd_int_t n) {
	struct kstr *rv = NULL;
	char *buf = vm_zalloc(vm, n);
	int rvl = fread(buf, 1, n, self->fp);
	if (rvl <= 0)
		goto done;
	rv = ymd_kstr(vm, buf, rvl);
done:
	vm_free(vm, buf);
	return rv;
}

static struct kstr *ansic_file_readall(struct ymd_mach *vm,
                                       struct ansic_file *self) {
	ymd_int_t len;
	fseek(self->fp, 0, SEEK_END);
	len = ftell(self->fp);
	rewind(self->fp);
	return ansic_file_readn(vm, self, len);
}

static struct kstr *ansic_file_readline(struct ymd_mach *vm,
                                        struct ansic_file *self) {
	char line[1024];
	char *rv = fgets(line, sizeof(line), self->fp);
	if (!rv)
		return NULL;
	return ymd_kstr(vm, line, -1);
}

static int ansic_file_read(struct ymd_context *l) {
	struct kstr *rv = NULL;
	struct variable *arg1 = NULL;
	struct ansic_file *self = mand_land(l->vm, ymd_argv_get(l, 0),
	                                    T_STREAM);
	if (ymd_argv_chk(l, 1)->count == 1) {
		rv = ansic_file_readn(l->vm, self, 128);
		goto done;
	}
	arg1 = ymd_argv_get(l, 1);
	switch (arg1->type) {
	case T_KSTR:
		if (strcmp(kstr_k(arg1)->land, "*all") == 0)
			rv = ansic_file_readall(l->vm, self);
		else if (strcmp(kstr_k(arg1)->land, "*line") == 0)
			rv = ansic_file_readline(l->vm, self);
		else
			vm_die(l->vm, "Bad read() option: %s", kstr_k(arg1)->land);
		break;
	case T_INT: {
		ymd_int_t n = int_of(l->vm, ymd_argv_get(l, 1));
		rv = ansic_file_readn(l->vm, self, n);
		} break;
	default:
		vm_die(l->vm, "Bad read() option");
		break;
	}
done:
	if (!rv)
		vset_nil(ymd_push(l));
	else
		vset_kstr(ymd_push(l), rv);
	return 1;
}

static int ansic_file_write(struct ymd_context *l) {
	struct ansic_file *self = mand_land(l->vm, ymd_argv_get(l, 0),
	                                    T_STREAM);
	struct kstr *bin = NULL;
	if (is_nil(ymd_argv_get(l, 1)))
		return 0;
	bin = kstr_of(l->vm, ymd_argv_get(l, 1));
	int rv = fwrite(bin->land, 1, bin->len, self->fp);
	if (rv < 0)
		vm_die(l->vm, "Write file failed!");
	return 0;
}

static int ansic_file_final(struct ansic_file *self) {
	if (self->fp) {
		fclose(self->fp);
		self->fp = NULL;
	}
	return 0;
}

static int libx_open(struct ymd_context *l) {
	const char *mod = "r";
	struct mand *x = mand_new(l->vm, sizeof(struct ansic_file),
	                          (ymd_final_t)ansic_file_final);
	struct ansic_file *rv = (struct ansic_file *)(x->land);
	x->tt = T_STREAM;
	rv->op.read = ansic_file_read;
	rv->op.write = ansic_file_write;
	if (ymd_argv_chk(l, 1)->count > 1)
		mod = kstr_of(l->vm, ymd_argv_get(l, 1))->land;
	rv->fp = fopen(kstr_of(l->vm, ymd_argv_get(l, 0))->land, mod);
	if (!rv->fp)
		vset_nil(ymd_push(l));
	else
		vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_read(struct ymd_context *l) {
	struct yio_stream *s = mand_land(l->vm, ymd_argv_get(l, 0), T_STREAM);
	return s->read(l);
}

static int libx_write(struct ymd_context *l) {
	struct yio_stream *s = mand_land(l->vm, ymd_argv_get(l, 0), T_STREAM);
	return s->write(l);
}

static int libx_close(struct ymd_context *l) {
	struct mand *pm = mand_of(l->vm, ymd_argv_get(l, 0));
	struct yio_stream *s = mand_land(l->vm, ymd_argv_get(l, 0), T_STREAM);
	assert(pm->final);
	return (*pm->final)(s);

}

//------------------------------------------------------------------------------
// Regex pattern and match
//------------------------------------------------------------------------------
static int posix_regex_final(struct posix_regex *self) {
	regfree(&self->core);
	return 0;
}

// Example:
// regex = pattern("^[0-9]+$")
// result = match(regex, "0000")
// print(result)
static int libx_pattern(struct ymd_context *l) {
	int err = 0, cflag = REG_EXTENDED;
	struct kstr *arg0 = kstr_of(l->vm, ymd_argv_get(l, 0));
	struct mand *x = mand_new(l->vm, sizeof(*x),
	                          (ymd_final_t)posix_regex_final);
	struct posix_regex *self = (struct posix_regex *)x->land;
	x->tt = T_REGEX;
	switch (ymd_argv(l)->count) {
	case 1:
		cflag = REG_NOSUB;
		break;
	case 2: {
		struct kstr *arg1 = kstr_of(l->vm, ymd_argv_get(l, 1));
		if (strcmp(arg1->land, "*nosub") == 0)
			cflag |= REG_NOSUB;
		else if (strcmp(arg1->land, "*sub") == 0)
			self->sub = 1;
		else
			vm_die(l->vm, "Bad regex option: %s", arg1->land);
		} break;
	default:
		vm_die(l->vm, "Too many args, %d", ymd_argv(l)->count);
		break;
	}
	err = regcomp(&self->core, arg0->land, cflag);
	if (err)
		vset_nil(ymd_push(l));
	else
		vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_match(struct ymd_context *l) {
	struct posix_regex *self = mand_land(l->vm, ymd_argv_get(l, 0),
	                                     T_REGEX);
	struct kstr *arg1 = kstr_of(l->vm, ymd_argv_get(l, 1));
	int err = 0;
	if (self->sub) {
		struct dyay *rv = dyay_new(l->vm, 0);
		regmatch_t matched[128];
		err = regexec(&self->core, arg1->land,
		              sizeof(matched)/sizeof(matched[0]),
					  matched, 0);
		if (!err) {
			regmatch_t *i;
			for (i = matched; i->rm_so != -1; ++i) {
				struct kstr *sub = ymd_kstr(l->vm, arg1->land + i->rm_so,
				                            i->rm_eo - i->rm_so);
				vset_kstr(dyay_add(l->vm, rv), sub);
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

static int libx_import(struct ymd_context *l) {
	int i;
	char blknam[MAX_BLOCK_NAME_LEN];
	struct func *block;
	struct kstr *name = kstr_of(l->vm, ymd_argv_get(l, 0));
	FILE *fp = fopen(name->land, "r");
	if (!fp)
		vm_die(l->vm, "Can not open import file: %s", name->land);
	block = ymd_compilef(l->vm,
		file2blknam(name->land, blknam, sizeof(blknam)), name->land, fp);
	fclose(fp);
	if (!block)
		vm_die(l->vm, "Import fatal, syntax error in file: `%s`", name->land);
	vset_func(ymd_push(l), block);
	for (i = 1; i < ymd_argv(l)->count; ++i)
		*ymd_push(l) = *ymd_argv_get(l, i);
	return ymd_call(l, block, ymd_argv(l)->count - 1, 0);
}

static int libx_eval(struct ymd_context *l) {
	int i;
	struct func *chunk;
	struct kstr *script = kstr_of(l->vm, ymd_argv_get(l, 0));
	chunk = ymd_compile(l->vm, "__blk_eval__", NULL, script->land);
	if (!chunk) {
		vset_nil(ymd_push(l));
		return 1;
	}
	vset_func(ymd_push(l), chunk);
	for (i = 1; i < ymd_argv(l)->count; ++i)
		*ymd_push(l) = *ymd_argv_get(l, i);
	return ymd_call(l, chunk, ymd_argv(l)->count - 1, 0);
}

static int libx_compile(struct ymd_context *l) {
	struct func *chunk;
	struct kstr *script = kstr_of(l->vm, ymd_argv_get(l, 0));
	chunk = ymd_compile(l->vm, "__blk_compile__", NULL, script->land);
	if (!chunk) {
		vset_nil(ymd_push(l));
		return 1;
	}
	vset_func(ymd_push(l), chunk);
	return 1;
}

static int libx_env(struct ymd_context *l) {
	struct kstr *which = kstr_of(l->vm, ymd_argv_get(l, 0));
	if (strcmp(which->land, "*global") == 0)
		vset_hmap(ymd_push(l), l->vm->global);
	else if (strcmp(which->land, "*kpool") == 0)
		vset_hmap(ymd_push(l), l->vm->kpool);
	else if (strcmp(which->land, "*current") == 0)
		vset_func(ymd_push(l), l->info->chain->run);
	else
		vset_nil(ymd_push(l));
	return 1;
}

static int libx_atoi(struct ymd_context *l) {
	struct kstr *arg0 = kstr_of(l->vm, ymd_argv_get(l, 0));
	int ok = 1;
	ymd_int_t i = dtoll(arg0->land, &ok);
	if (ok)
		vset_int(ymd_push(l), i);
	else
		vset_nil(ymd_push(l));
	return 1;
}

static int libx_exit(struct ymd_context *l) {
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


int ymd_load_lib(struct ymd_mach *vm, ymd_libc_t lbx) {
	const struct libfn_entry *i;
	int rv = 0;
	for (i = lbx; i->native != NULL; ++i) {
		vset_func(ymd_putg(vm, i->symbol.z),
		          func_new_c(vm, i->native, i->symbol.z));
		++rv;
	}
	return rv;
}

int ymd_load_mem(struct ymd_mach *vm, const char *clazz, void *o,
                 ymd_libc_t lbx) {
	int rv = 0;
	const struct libfn_entry *i;
	for (i = lbx; i->native != NULL; ++i) {
		char name[128];
		struct func *method;
		strncpy(name, clazz, sizeof(name));
		strcat(name, ".");
		strcat(name, i->symbol.z);
		method = func_new_c(vm, i->native, name);
		vset_func(ymd_def(vm, o, i->symbol.z), method);
		++rv;
	}
	return rv;
}

