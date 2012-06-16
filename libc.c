#include "state.h"
#include "value.h"
#include "memory.h"
#include "libc.h"
#include <stdio.h>

struct strbuf {
	int len;
	int max;
	char *buf;
};
#define STRBUF_ADD 128
static const char *T_STRBUF = "strbuf";

struct ansic_file {
	struct yio_stream op;
	FILE *fp;
};
static const char *T_STREAM = "stream";

#define PRINT_SPLIT " "
static void *const kend = (void *)-1;

static int print_var(const struct variable *var) {
	switch (var->type) {
	case T_NIL:
		printf("nil");
		break;
	case T_BOOL:
		printf("%s", var->value.i ? "true" : "false");
		break;
	case T_INT:
		printf("%lld", var->value.i);
		break;
	case T_EXT:
		printf("@%p", var->value.ext);
		break;
	case T_KSTR:
		printf("%s", kstr_k(var)->land);
		break;
	case T_FUNC:
		printf("%s", func_k(var)->proto->land);
		break;
	case T_DYAY: {
		int i;
		const struct dyay *arr = dyay_k(var);
		printf("{");
		for (i = 0; i < arr->count; ++i) {
			if (i > 0) printf(", ");
			print_var(arr->elem + i);
		}
		printf("}");
		} break;
	case T_SKLS: {
		int f = 0;
		const struct sknd *i = skls_k(var)->head;
		printf("@{");
		while ((i = i->fwd[0]) != NULL) {
			if (f++ > 0) printf(", ");
			print_var(&i->k);
			putchar(':');
			print_var(&i->v);
		}
		putchar('}');
		} break;
	case T_HMAP: {
		const struct hmap *map = hmap_k(var);
		const int k = 1 << map->shift;
		int i, f = 0;
		putchar('{');
		for (i = 0; i < k; ++i) {
			if (!map->item[i].flag)
				continue;
			if (f++ > 0) printf(", ");
			print_var(&map->item[i].k);
			putchar(':');
			print_var(&map->item[i].v);
		}
		putchar('}');
		} break;
	case T_MAND: {
		const struct mand *pm = mand_k(var);
		int i;
		printf("block:%p [", pm);
		for (i = 0; i < pm->len; ++i)
			printf("%02x ", (unsigned char)pm->land[i]);
		printf("]");
		} break;
	default:
		vm_die("Bad type: %d", var->type);
		break;
	}
	return 0;
}

static int libx_print(struct context *l) {
	int i;
	struct dyay *argv = ymd_argv(l);
	if (!argv)
		goto out;
	for (i = 0; i < argv->count; ++i) {
		struct variable *var = argv->elem + i;
		if (i > 0) printf(PRINT_SPLIT);
		print_var(var);
	}
out:
	printf("\n");
	return 0;
}

static int libx_insert(struct context *l) {
	int i;
	struct dyay *self, *argv = ymd_argv(l);
	if (!argv)
		return 0;
	self = dyay_of(argv->elem + 0);
	for (i = 1; i < argv->count; ++i)
		*dyay_add(self) = argv->elem[i];
	return 0;
}

static int libx_len(struct context *l) {
	const struct variable *arg0 = ymd_argv_get(l, 0);
	switch (arg0->type) {
	case T_KSTR:
		ymd_push_int(l, kstr_k(arg0)->len);
		break;
	case T_DYAY:
		ymd_push_int(l, dyay_k(arg0)->count);
		break;
	default:
		ymd_push_nil(l);
		vm_die("Bad argument 1, need a string/hashmap/skiplist/array");
		return 1;
	}
	return 1;
}

static int libx_str(struct context *l) {
	// TODO:
	(void)l;
	return 0;
}

//------------------------------------------------------------------------------
// Foreach closures
//------------------------------------------------------------------------------
static int libx_end(struct context *l) {
	vset_ext(ymd_push(l), kend);
	return 1;
}

static int step_iter(struct context *l) {
	struct variable *i = ymd_bval(l, 0),
					*m = ymd_bval(l, 1),
					*s = ymd_bval(l, 2),
					rv;
	rv.type = T_EXT;
	rv.value.ext = kend;
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
		return func_new(libx_end);
	iter = func_new(step_iter);
	iter->n_bind = 3;
	func_init(iter);
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
		vset_ext(ymd_push(l), kend);
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
		vset_ext(ymd_push(l), kend);
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
		vset_ext(ymd_push(l), kend);
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
		iter = func_new(dyay_iter);
		iter->n_bind = 4;
		func_init(iter);
		vset_ext(func_bval(iter, 0), dyay_k(obj)->elem);
		vset_ext(func_bval(iter, 1), dyay_k(obj)->elem + dyay_k(obj)->count);
		vset_int(func_bval(iter, 2), 0);
		vset_int(func_bval(iter, 3), flag);
		break;
	case T_HMAP: {
		struct kvi *m = hmap_k(obj)->item + (1 << hmap_k(obj)->shift),
				   *i = move2valid(hmap_k(obj)->item, m);
		if (i >= m)
			return func_new(libx_end);
		iter = func_new(hmap_iter);
		iter->n_bind = 3;
		func_init(iter);
		vset_ext(func_bval(iter, 0), i);
		vset_ext(func_bval(iter, 1), m);
		vset_int(func_bval(iter, 2), flag);
		} break;
	case T_SKLS: {
		struct sknd *i = skls_k(obj)->head->fwd[0];
		if (!i)
			return func_new(libx_end);
		iter = func_new(skls_iter);
		iter->n_bind = 2;
		func_init(iter);
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

static int libx_done(struct context *l) {
	const struct dyay *argv = ymd_argv_chk(l, 1);
	if (argv->elem[0].type == T_EXT &&
		argv->elem[0].value.ext == kend)
		ymd_push_bool(l, 1);
	else
		ymd_push_bool(l, 0);
	return 1;
}

static int libx_panic(struct context *l) {
	(void)l;
	vm_die("Unknown");
	return 0;
}

//------------------------------------------------------------------------------
// String Buffer: strbuf
//------------------------------------------------------------------------------
static inline struct strbuf *strbuf_of(struct mand *pm) {
	if (pm->tt != T_STRBUF)
		vm_die("Builtin fatal:%s:%d", __FILE__, __LINE__);
	return (struct strbuf *)pm->land;
}

static int strbuf_final(struct strbuf *sb) {
	if (sb->buf)
		vm_free(sb->buf);
	sb->len = 0;
	sb->max = 0;
	sb->buf = NULL;
	return 0;
}

static void strbuf_need(struct strbuf *sb, int add) {
	if (sb->len + add <= sb->max)
		return;
	while (sb->len + add > sb->max)
		sb->max = sb->max * 3 / 2 + STRBUF_ADD;
	if (!sb->buf)
		sb->buf = vm_zalloc(sb->max);
	else
		sb->buf = vm_realloc(sb->buf, sb->max);
}

static inline void strbuf_append(struct strbuf *sb,
                                 const char *z, int n) {
	assert(sb->len + n <= sb->max);
	memcpy(sb->buf + sb->len, z, n);
	sb->len += n;
}

static int libx_strbuf(struct context *l) {
	struct mand *x = mand_new(NULL, sizeof(struct strbuf),
	                          (ymd_final_t)strbuf_final);
	x->tt = T_STRBUF;
	vset_mand(ymd_push(l), x);
	return 1;
}

static int libx_strfin(struct context *l) {
	struct strbuf *self;
	struct kstr *rv;
	self = strbuf_of(mand_of(ymd_argv_get(l, 0)));
	if (self->len == 0) {
		vset_nil(ymd_push(l));
		return 0;
	}
	rv = ymd_kstr(self->buf, self->len);
	strbuf_final(self);
	vset_kstr(ymd_push(l), rv);
	return 1;
}

static int libx_strcat(struct context *l) {
	int i;
	struct strbuf *self;
	struct dyay *argv = ymd_argv_chk(l, 2);
	self = strbuf_of(mand_of(argv->elem));
	for (i = 1; i < argv->count; ++i) {
		struct kstr *kz = kstr_of(argv->elem + i);
		strbuf_need(self, kz->len);
		strbuf_append(self, kz->land, kz->len);
	}
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

LIBC_BEGIN(Builtin)
	LIBC_ENTRY(print)
	LIBC_ENTRY(insert)
	LIBC_ENTRY(len)
	LIBC_ENTRY(range)
	LIBC_ENTRY(rank)
	LIBC_ENTRY(ranki)
	LIBC_ENTRY(end)
	LIBC_ENTRY(done)
	LIBC_ENTRY(str)
	LIBC_ENTRY(panic)
	LIBC_ENTRY(strbuf)
	LIBC_ENTRY(strcat)
	LIBC_ENTRY(strfin)
	LIBC_ENTRY(open)
	LIBC_ENTRY(read)
	LIBC_ENTRY(write)
	LIBC_ENTRY(close)
LIBC_END


int ymd_load_lib(ymd_libc_t lbx) {
	const struct libfn_entry *i;
	for (i = lbx; i->native != NULL; ++i) {
		struct kstr *kz = ymd_kstr(i->symbol.z, i->symbol.len);
		struct func *fn = func_new(i->native);
		struct variable key, *rv;
		key.type = T_KSTR;
		key.value.ref = gcx(kz);
		rv = hmap_get(vm()->global, &key);
		rv->type = T_FUNC;
		func_init(fn);
		rv->value.ref = gcx(fn);
	}
	return 0;
}
