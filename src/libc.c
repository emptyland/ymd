#include "third_party/pcre/pcre.h"
#include "tostring.h"
#include "core.h"
#include "compiler.h"
#include "encoding.h"
#include "libc.h"
#include <stdio.h>
#include <setjmp.h>

struct pcre_regex {
	pcre *core;
	pcre_extra *extra;
};

struct ansic_file {
	FILE *fp;
};

static const char *T_REGEX = "regex";
static const char *T_STRBUF = "strbuf";
static const char *T_STREAM = "stream";

#define PRINT_SPLIT " "

#define L struct ymd_context *l

static int libx_print(L) {
	int i, argc = ymd_argc(l);
	struct zostream os = ZOS_INIT;
	if (!argc) { puts(""); return 0; }
	for (i = 0; i < argc; ++i) {
		const char *raw = tostring(&os, ymd_argv(l, i));
		if (i > 0) printf(PRINT_SPLIT);
		fwrite(raw, os.last, 1, stdout);
		zos_final(&os);
	}
	puts("");
	return 0;
}

static void do_insert(L, struct dyay *self) {
	struct variable *elem;
	switch (ymd_argc(l)) {
	case 2:
		elem = ymd_argv(l, 1);
		if (is_nil(elem))
			ymd_panic(l, "Element of array can not be `nil`");
		setv_dyay(ymd_push(l), self);
			*ymd_push(l) = *elem; ymd_add(l);
		ymd_pop(l, 1);
		break;
	case 3: {
		ymd_int_t i = int_of(l, ymd_argv(l, 1));
		if (i < 0 || i >= self->count)
			ymd_panic(l, "array index out of range, %lld", i);
		elem = ymd_argv(l, 2);
		if (is_nil(elem))
			ymd_panic(l, "Element in array can not be `nil`");
		*dyay_insert(l->vm, self, i) = *elem;
		} break;
	default:
		ymd_panic(l, "Too many arguments, %d", ymd_argc(l));
		break;
	}
}

static void checked_put(L, struct variable *arg0,
                        const struct variable *k,
						const struct variable *v) {
	if (is_nil(k) || is_nil(v))
		ymd_panic(l, "Value can not be `nil` in k-v pair");
	switch (ymd_type(arg0)) {
	case T_HMAP:
		*hmap_put(l->vm, hmap_x(arg0), k) = *v;
		break;
	case T_SKLS:
		*skls_put(l->vm, skls_x(arg0), k) = *v;
		break;
	default:
		ymd_panic(l, "insert() don't support `%s'",
				typeof_kz(ymd_type(arg0)));
		break;
	}
}

// insert to array:
//     insert(array, elem)
//     insert(array, position, elem)
// insert to map:
//     insert(map, key, value)
//
static int libx_insert(L) {
	struct variable *arg0 = ymd_argv(l, 0);
	switch (ymd_type(arg0)) {
	case T_DYAY:
		do_insert(l, dyay_x(arg0));
		break;
	default:
		checked_put(l, arg0, ymd_argv(l, 1), ymd_argv(l, 2));
		break;
	}
	return 0;
}

static int libx_append(L) {
	int i;
	struct variable *arg0 = ymd_argv(l, 0);
	switch (ymd_type(arg0)) {
	case T_DYAY:
		for (i = 1; i < ymd_argc(l); ++i)
			*dyay_add(l->vm, dyay_x(arg0)) = *ymd_argv(l, i);
		break;
	case T_HMAP:
		for (i = 1; i < ymd_argc(l); ++i) {
			struct dyay *pair = dyay_of(l, ymd_argv(l, i));
			*hmap_put(l->vm, hmap_x(arg0), pair->elem) = pair->elem[1];
		}
		break;
	case T_SKLS:
		for (i = 1; i < ymd_argc(l); ++i) {
			struct dyay *pair = dyay_of(l, ymd_argv(l, i));
			*skls_put(l->vm, skls_x(arg0), pair->elem) = pair->elem[1];
		}
		break;
	default:
		ymd_panic(l, "append() don't support `%s'",
				typeof_kz(ymd_type(arg0)));
		break;
	}
	return 0;
}

static int libx_remove(L) {
	int i, count = 0;
	struct variable *arg0 = ymd_argv(l, 0);
	for (i = 1; i < ymd_argc(l); ++i)
		count += vm_remove(l->vm, arg0, ymd_argv(l, i));
	ymd_int(l, count);
	return 1;
}

// len(arg)
// argument follow:
// nil      : always be 0;
// string   : length;
// array    : number of elements; 
// hashmap  : number of k-v pairs;
// skiplist : number of k-v pairs;
static int libx_len(L) {
	const struct variable *arg0 = ymd_argv(l, 0);
	switch (ymd_type(arg0)) {
	case T_NIL:
		ymd_int(l, 0);
		break;
	case T_KSTR:
		ymd_int(l, kstr_k(arg0)->len);
		break;
	case T_DYAY:
		ymd_int(l, dyay_k(arg0)->count);
		break;
	case T_HMAP: {
		ymd_int_t n = 0;
		struct kvi *i = hmap_k(arg0)->item,
				   *k = i + (1 << hmap_k(arg0)->shift);
		for (; i != k; ++i)
			if (i->flag) ++n;
		ymd_int(l, n);
		} break;
	case T_SKLS: {
		ymd_int_t n = 0;
		struct sknd *i = skls_k(arg0)->head->fwd[0];
		for (; i != NULL; i = i->fwd[0]) ++n;
		ymd_int(l, n);
		} break;
	default:
		ymd_panic(l, "len() don't support `%s', need a container or "
				"string type", typeof_kz(ymd_type(arg0)));
		return 0;
	}
	return 1;
}

// int 12   -> "12"
// float 1.1-> "1.1000"
// lite 12  -> "@0x000000000000000C"
// nil      -> "nil"
// bool     -> "true" or "false"
// string   -> "Hello, World"
// array    -> "{1,2,name,{1,2}}"
// function -> "func (...) {...}"
// hashmap  -> "{name:John, content:{1,2,3}}"
// skiplist -> "@{name:John, content:{1,2,3}}"
// managed  -> "(stream)[24@0x08067FF]"
static int libx_str(L) {
	const struct variable *arg0 = ymd_argv(l, 0);
	struct zostream os = ZOS_INIT;
	const char *z = tostring(&os, arg0);
	ymd_kstr(l, z, os.last);
	zos_final(&os);
	return 1;
}

//------------------------------------------------------------------------------
// Foreach closures
//------------------------------------------------------------------------------
static int libx_end(L) {
	(void)l;
	return 0;
}

static int step_iter(L) {
	struct variable *i = ymd_upval(l, 0),
					*m = ymd_upval(l, 1),
					*s = ymd_upval(l, 2),
					rv;
	setv_nil(&rv);
	if (s->u.i > 0 && i->u.i < m->u.i) {
		setv_int(&rv, i->u.i);
	} else if (s->u.i < 0 && i->u.i > m->u.i) {
		setv_int(&rv, i->u.i);
	}
	i->u.i += s->u.i;
	*ymd_push(l) = rv;
	return 1;
}

static int new_step_iter(L, ymd_int_t i, ymd_int_t m, ymd_int_t s) {
	if ((s == 0) || (i > m && s > 0) || (i < m && s < 0) || (i == m)) {
		ymd_nafn(l, libx_end, "end", 0);
		return 1;
	}
	ymd_nafn(l, step_iter, "__step_iter__", 3);
	ymd_int(l, i); ymd_bind(l, 0);
	ymd_int(l, m); ymd_bind(l, 1);
	ymd_int(l, s); ymd_bind(l, 2);
	return 1;
}

#define ITER_KEY   0
#define ITER_VALUE 1
#define ITER_KV    2

// Dynamic array iterator
// bind[0]: struct variable *i; current pointer
// bind[1]: struct variable *m; end of pointer
// bind[2]: int idx; index counter
// bind[3]: iterator flag
// bind[4]: dyay self
static int dyay_iter(L) {
	struct variable *i = ymd_upval(l, 0)->u.ext,
	                *m = ymd_upval(l, 1)->u.ext;
	if (i >= m)
		return 0;
	switch (int_of(l, ymd_upval(l, 3))) {
	case ITER_KEY: {
		ymd_int_t idx = int_of(l, ymd_upval(l, 2));
		ymd_int(l, idx);
		setv_int(ymd_upval(l, 2), idx + 1);
		} break;
	case ITER_VALUE: {
		*ymd_push(l) = *i;
		} break;
	case ITER_KV: {
		ymd_int_t idx = int_of(l, ymd_upval(l, 2));
		ymd_dyay(l, 2);
		ymd_int(l, idx);
		ymd_add(l);
		*ymd_push(l) = *i;
		ymd_add(l);
		setv_int(ymd_upval(l, 2), idx + 1);
		} break;
	default:
		assert(!"No reached.");
		break;
	}
	setv_ext(ymd_upval(l, 0), i + 1);
	return 1;
}

// Move hash map's slot to next valid one:
static inline struct kvi *move2valid(struct kvi *i, struct kvi *m) {
	while (i < m && i->flag == 0) ++i;
	return i;
}

// Hash map iterator
// bind[0]: struct kvi *i; current pointer
// bind[1]: struct kvi *m; end of pointer
// bind[2]: iterator flag
static int hmap_iter(L) {
	struct kvi *i = ymd_upval(l, 0)->u.ext,
			   *m = ymd_upval(l, 1)->u.ext;
	if (i >= m) {
		setv_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(l, ymd_upval(l, 2))) {
	case ITER_KEY:
		*ymd_push(l) = i->k;
		break;
	case ITER_VALUE:
		*ymd_push(l) = i->v;
		break;
	case ITER_KV: {
		ymd_dyay(l, 2);
		*ymd_push(l) = i->k; ymd_add(l);
		*ymd_push(l) = i->v; ymd_add(l);
		} break;
	default:
		assert(!"No reached.");
		break;
	}
	setv_ext(ymd_upval(l, 0), move2valid(i + 1, m));
	return 1;
}

// Skip list iterator
// bind[0]: struct sknd *i; current pointer
// bind[1]: iterator flag
static int skls_iter(L) {
	struct sknd *i = ymd_upval(l, 0)->u.ext;
	if (!i) {
		setv_nil(ymd_push(l));
		return 1;
	}
	switch (int_of(l, ymd_upval(l, 1))) {
	case ITER_KEY:
		*ymd_push(l) = i->k;
		break;
	case ITER_VALUE:
		*ymd_push(l) = i->v;
		break;
	case ITER_KV: {
		ymd_dyay(l, 2);
		*ymd_push(l) = i->k; ymd_add(l);
		*ymd_push(l) = i->v; ymd_add(l);
		} break;
	default:
		assert(!"No reached.");
		break;
	}
	setv_ext(ymd_upval(l, 0), i->fwd[0]);
	return 1;
}

static int new_contain_iter(L, const struct variable *obj, int flag) {
	switch (ymd_type(obj)) {
	case T_DYAY:
		ymd_nafn(l, dyay_iter, "__dyay_iter__", 5);
		ymd_ext(l, dyay_k(obj)->elem);
		ymd_bind(l, 0);
		ymd_ext(l, dyay_k(obj)->elem + dyay_k(obj)->count);
		ymd_bind(l, 1);
		ymd_int(l, 0);
		ymd_bind(l, 2);
		ymd_int(l, flag);
		ymd_bind(l, 3);
		setv_ref(ymd_push(l), obj->u.ref);
		ymd_bind(l, 4);
		return 1;
	case T_HMAP: {
		struct kvi *m = hmap_k(obj)->item + (1 << hmap_k(obj)->shift),
				   *i = move2valid(hmap_k(obj)->item, m);
		if (i >= m) {
			ymd_nafn(l, libx_end, "end", 0);
			return 1;
		}
		ymd_nafn(l, hmap_iter, "__hmap_iter__", 4);
		ymd_ext(l, i);
		ymd_bind(l, 0);
		ymd_ext(l, m);
		ymd_bind(l, 1);
		ymd_int(l, flag);
		ymd_bind(l, 2);
		setv_ref(ymd_push(l), obj->u.ref);
		ymd_bind(l, 3);
		} return 1;
	case T_SKLS: {
		struct sknd *i = skls_k(obj)->head->fwd[0];
		if (!i) {
			ymd_nafn(l, libx_end, "end", 0);
			return 1;
		}
		ymd_nafn(l, skls_iter, "__skls_iter__", 3);
		ymd_ext(l, i);
		ymd_bind(l, 0);
		ymd_int(l, flag);
		ymd_bind(l, 1);
		setv_ref(ymd_push(l), obj->u.ref);
		ymd_bind(l, 2);
		} return 1;
	default:
		ymd_panic(l, "Type is not be supported");
		break;
	}
	return 0;
}

// range(begin, end, [step])
// Example:
// range(1,100) = 1,2,3,...99
// range(1,100,2) = 1,3,5,...99
// range([9,8,7]) = 9,8,7
static int libx_range(L) {
	switch (ymd_argc(l)) {
	case 2: {
		ymd_int_t i = int_of(l, ymd_argv(l, 0)),
				  m = int_of(l, ymd_argv(l, 1));
		return new_step_iter(l, i, m, i > m ? -1 : +1);
		}
	case 3:
		return new_step_iter(l, int_of(l, ymd_argv(l, 0)),
		                     int_of(l, ymd_argv(l, 1)),
		                     int_of(l, ymd_argv(l, 2)));
	default:
		ymd_panic(l, "Bad arguments, need 2 to 3");
		break;
	}
	return 0;
}

static int libx_values(L) {
	return new_contain_iter(l, ymd_argv(l, 0), ITER_VALUE);
}

static int libx_pairs(L) {
	return new_contain_iter(l, ymd_argv(l, 0), ITER_KV);
}

static int libx_keys(L) {
	return new_contain_iter(l, ymd_argv(l, 0), ITER_KEY);
}

static int libx_panic(L) {
	int i, argc = ymd_argc(l);
	struct zostream os = ZOS_INIT;
	if (!argc)
		ymd_panic(l, "Unknown");
	for (i = 0; i < argc; ++i) {
		if (i > 0) zos_append(&os, " ", 1);
		tostring(&os, ymd_argv(l, i));
	}
	ymd_panic(l, zos_buf(&os));
	zos_final(&os);
	return 0;
}

//------------------------------------------------------------------------------
// String Buffer: strbuf
//------------------------------------------------------------------------------
static int strbuf_final(struct zostream *sb) {
	zos_final(sb);
	return 0;
}

static int libx_cat(L) {
	int i, argc = ymd_argc(l);
	struct zostream *self = mand_land(l, ymd_argv(l, 0), T_STRBUF);
	if (argc < 2)
		ymd_panic(l, "strbuf:cat() need more than 2 arguments.");
	for (i = 1; i < argc; ++i)
		tostring(self, ymd_argv(l, i));
	*ymd_push(l) = *ymd_argv(l, 0);
	return 1;
}

static int libx_get(L) {
	struct zostream *self = mand_land(l, ymd_argv(l, 0), T_STRBUF);
	if (self->last == 0)
		return 0;
	ymd_kstr(l, zos_buf(self), self->last);
	return 1;
}

static int libx_clear(L) {
	strbuf_final(mand_land(l, ymd_argv(l, 0), T_STRBUF));
	return 0;
}

LIBC_BEGIN(StringBuffer)
	LIBC_ENTRY(cat)
	LIBC_ENTRY(get)
	LIBC_ENTRY(clear)
LIBC_END

static int libx_strbuf(L) {
	struct zostream *self = ymd_mand(l, T_STRBUF, sizeof(*self),
	                                 (ymd_final_t)strbuf_final);
	self->max = MAX_STATIC_LEN;
	ymd_skls(l, SKLS_ASC); // FIXME:
	ymd_load_mem(l, "__buitin__.strbuf", lbxStringBuffer);
	ymd_setmetatable(l);
	return 1;
}

//------------------------------------------------------------------------------
// File
//------------------------------------------------------------------------------
static int ansic_file_final(struct ansic_file *self) {
	if (self->fp) {
		fclose(self->fp);
		self->fp = NULL;
	}
	return 0;
}

static int ansic_file_readn(L, struct ansic_file *self, ymd_int_t n) {
	char *buf = vm_zalloc(l->vm, n);
	int rvl = fread(buf, 1, n, self->fp);
	if (rvl <= 0) {
		vm_free(l->vm, buf);
		return 0;
	}
	ymd_kstr(l, buf, rvl);
	vm_free(l->vm, buf);
	return 1;
}

static int ansic_file_readall(L, struct ansic_file *self) {
	ymd_int_t len;
	fseek(self->fp, 0, SEEK_END);
	len = ftell(self->fp);
	rewind(self->fp);
	return ansic_file_readn(l, self, len);
}

static int ansic_file_readline(L, struct ansic_file *self) {
	char line[1024];
	char *rv = fgets(line, sizeof(line), self->fp);
	if (!rv)
		return 0;
	ymd_kstr(l, line, -1);
	return 1;
}

static int libx_read(L) {
	struct variable *arg1;
	struct ansic_file *self = mand_land(l, ymd_argv(l, 0),
	                                    T_STREAM);
	if (ymd_argc(l) == 1)
		return ansic_file_readn(l, self, 128);
	arg1 = ymd_argv(l, 1);
	switch (ymd_type(arg1)) {
	case T_KSTR:
		if (strcmp(kstr_k(arg1)->land, "*all") == 0)
			return ansic_file_readall(l, self);
		else if (strcmp(kstr_k(arg1)->land, "*line") == 0)
			return ansic_file_readline(l, self);
		else
			ymd_panic(l, "Bad read() option: %s", kstr_k(arg1)->land);
		break;
	case T_INT:
		return ansic_file_readn(l, self,
		                        int_of(l, ymd_argv(l, 1)));
	default:
		ymd_panic(l, "Bad read() option");
		break;
	}
	return 0;
}

static int libx_write(L) {
	struct kstr *bin = NULL;
	struct ansic_file *self = mand_land(l, ymd_argv(l, 0),
	                                    T_STREAM);
	if (is_nil(ymd_argv(l, 1)))
		return 0;
	bin = kstr_of(l, ymd_argv(l, 1));
	int rv = fwrite(bin->land, 1, bin->len, self->fp);
	if (rv < 0)
		ymd_panic(l, "Write file failed!");
	return 0;
}

static int libx_close(L) {
	struct ansic_file *self = mand_land(l, ymd_argv(l, 0),
	                                    T_STREAM);
	ansic_file_final(self);
	return 0;
}

LIBC_BEGIN(ANSICFile)
	LIBC_ENTRY(read)
	LIBC_ENTRY(write)
	LIBC_ENTRY(close)
LIBC_END

static int libx_open(L) {
	const char *mod = "r";
	struct ansic_file *self;
	self = ymd_mand(l, T_STREAM, sizeof(*self),
	                (ymd_final_t)ansic_file_final);
	if (ymd_argc(l) > 1)
		mod = kstr_of(l, ymd_argv(l, 1))->land;
	self->fp = fopen(kstr_of(l, ymd_argv(l, 0))->land, mod);
	if (!self->fp) {
		ymd_pop(l, 1);
		return 0;
	}
	ymd_skls(l, SKLS_ASC); // FIXME:
	ymd_load_mem(l, "__buitin__.file", lbxANSICFile);
	ymd_setmetatable(l);
	return 1;
}

//------------------------------------------------------------------------------
// Regex pattern and match
//------------------------------------------------------------------------------
static int pcre_regex_final(struct pcre_regex *re) {
	pcre_free_study(re->extra);
	pcre_free(re->core);
	return 0;
}

// Example:
// regex = pattern("^\\d+$")
// result = match(regex, "0000")
// print(result)
static int libx_pattern(L) {
	const char *err;
	int err_off;
	struct kstr *arg0 = kstr_of(l, ymd_argv(l, 0));
	struct pcre_regex *re = ymd_mand(l, T_REGEX, sizeof(*re),
			(ymd_final_t)pcre_regex_final);
	re->core = pcre_compile(arg0->land, PCRE_UTF8, &err, &err_off, NULL);
	if (!re->core) {
		ymd_pop(l, 1);
		ymd_panic(l, "Regex compile fatal: %s, near: %s",
				err, arg0->land);
	}
	re->extra = pcre_study(re->core, PCRE_STUDY_JIT_COMPILE, &err);
	if (!re->extra) {
		ymd_pop(l, 1);
		ymd_panic(l, "Regex extra fatal: %s", err);
	}
	vm_pcre_lazy(l->vm);
	return 1;
}

static int libx_match(L) {
	const char **list;
	struct pcre_regex *re = mand_land(l, ymd_argv(l, 0), T_REGEX);
	struct kstr *arg1 = kstr_of(l, ymd_argv(l, 1));
	int i, ovec[128] = {0},
		rv = pcre_jit_exec(re->core, re->extra, arg1->land, arg1->len,
				0, 0, ovec, ARRAY_SIZEOF(ovec), l->vm->pcre_js);
	if (rv <= 0)
		return 0;
	if (rv == 1) {
		ymd_bool(l, 1);
		return 1;
	}
	pcre_get_substring_list(arg1->land, ovec, rv, &list);
	ymd_dyay(l, rv);
	i = rv;
	while (i--) {
		ymd_kstr(l, list[rv - i - 1], -1);
		ymd_add(l);
	}
	pcre_free_substring_list(list);
	return 1;
}

// aaaa^bbb^ccc
// [5, 6]
// 
static int libx_split(L) {
	struct kstr *arg0 = kstr_of(l, ymd_argv(l, 0));
	struct pcre_regex *re = mand_land(l, ymd_argv(l, 1), T_REGEX);
	int ovec[3] = {0}, i = 0, len = arg0->len, rv;
	ymd_dyay(l, 0);
	while ((rv = pcre_jit_exec(
					re->core, re->extra, arg0->land + i, len, 0, 0,
					ovec, ARRAY_SIZEOF(ovec), l->vm->pcre_js)) > 0) {
		if (ovec[1] == 0)
			return 1;
		ymd_kstr(l, arg0->land + i, ovec[0]);
		ymd_add(l);
		len -= ovec[1];
		i   += ovec[1];
	}
	if (i < arg0->len) {
		ymd_kstr(l, arg0->land + i, arg0->len - i);
		ymd_add(l);
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
		} else if (ch == '/') {
			strcat(tran + i, "_4d_");
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

static int libx_import(L) {
	int i;
	FILE *fp;
	char blknam[MAX_BLOCK_NAME_LEN];
	struct kstr *name = kstr_of(l, ymd_argv(l, 0));
	if (vm_loaded(l->vm, name->land))
		return 0;
	fp = fopen(name->land, "r");
	if (!fp)
		ymd_panic(l, "Can not open file: %s", name->land);
	i = ymd_compilef(l, file2blknam(name->land, blknam, sizeof(blknam)),
	                 name->land, fp);
	fclose(fp);
	if (i < 0)
		ymd_panic(l, "Import fatal, syntax error in file: `%s`",
		       name->land);
	for (i = 1; i < ymd_argc(l); ++i)
		*ymd_push(l) = *ymd_argv(l, i);
	return ymd_call(l, func_of(l, ymd_top(l, 0)), ymd_argc(l) - 1, 0);
}

static int libx_eval(L) {
	int i;
	struct func *fn;
	struct kstr *script = kstr_of(l, ymd_argv(l, 0));
	i = ymd_compile(l, "__blk_eval__", "[chunk]", script->land);
	if (i < 0)
		return 0;
	fn = func_of(l, ymd_top(l, 0));
	for (i = 1; i < ymd_argc(l); ++i)
		*ymd_push(l) = *ymd_argv(l, i);
	return ymd_call(l, fn, ymd_argc(l) - 1, 0);
}

static int libx_compile(L) {
	struct kstr *script = kstr_of(l, ymd_argv(l, 0));
	int i = ymd_compile(l, "__blk_compile__", "[chunk]", script->land);
	return (i < 0) ? 0 : 1;
}

static int libx_env(L) {
	struct kstr *which = kstr_of(l, ymd_argv(l, 0));
	if (strcmp(which->land, "*global") == 0)
		setv_hmap(ymd_push(l), l->vm->global);
	else if (strcmp(which->land, "*current") == 0)
		setv_func(ymd_push(l), l->info->chain->run);
	else
		setv_nil(ymd_push(l));
	return 1;
}

static int libx_atoi(L) {
	struct kstr *arg0 = kstr_of(l, ymd_argv(l, 0));
	int ok = 1;
	ymd_int_t i = dtoll(arg0->land, &ok);
	if (!ok)
		return 0;
	ymd_int(l, i);
	return 1;
}

static int libx_atof(L) {
	struct kstr *arg0 = kstr_of(l, ymd_argv(l, 0));
	ymd_float(l, atof(arg0->land));
	return 1;
}

// Slice
// slice(string, start, count)
// slice(string, start) == slice(string, start, len(string) - start)
// slice(array, start, count)
// slice(array, start) == slice(array, start, len(array) - start)
// slice(skip_list, begin, end) -> skip_list[begin, end) 
static int libx_slice(L) {
	struct variable *arg0 = ymd_argv(l, 0);
	switch (ymd_type(arg0)) {
	case T_KSTR: {
		struct kstr *o = kstr_of(l, arg0);
		ymd_int_t start, count;
		if (ymd_argc(l) == 2) {
			start = int4of(l, ymd_argv(l, 1));
			count = o->len - start;
		} else {
			start = int4of(l, ymd_argv(l, 1));
			count = int4of(l, ymd_argv(l, 2));
		}
		count = YMD_MIN(count, o->len - start);
		if (count <= 0)
			ymd_kstr(l, "", 0);
		else
			ymd_kstr(l, o->land + start, count);
		} break;
	case T_DYAY: {
		struct dyay *o = dyay_of(l, arg0);
		ymd_int_t i, start, count;
		if (ymd_argc(l) == 2) {
			start = int4of(l, ymd_argv(l, 1));
			count = o->count - start;
		} else {
			start = int4of(l, ymd_argv(l, 1));
			count = int4of(l, ymd_argv(l, 2));
		}
		count = YMD_MIN(count, o->count - start);
		ymd_dyay(l, 0);
		if (count <= 0)
			break;
		for (i = start; i < start + count; ++i) {
			*ymd_push(l) = o->elem[i];
			ymd_add(l);
		}
		} break;
	case T_SKLS: {
		const struct skls *o = skls_of(l, arg0);
		const struct sknd *i, *k;
		struct variable *arg1;
		ymd_skls(l, o->cmp);
		if (ymd_argc(l) == 2) {
			arg1 = ymd_argv(l, 1);
			k = NULL; // to end
		} else {
			struct variable *arg2 = ymd_argv(l, 2);
			arg1 = ymd_argv(l, 1);
			// arg1 must be front of arg2 in the skip list. 
			if (skls_key_compare(l->vm, o, arg1, arg2) > 0) {
				struct variable *tmp = arg1;
				arg1 = arg2;
				arg2 = tmp;
			}
			k = skls_direct(l->vm, o, arg2);
		}
		// Make [i, k) range new skip list.
		for (i = skls_direct(l->vm, o, arg1); i != k; i = i->fwd[0]) {
			*ymd_push(l) = i->k;
			*ymd_push(l) = i->v;
			ymd_putf(l);
		}
		} break;
	default:
		ymd_panic(l, "slice() don't support `%s'",
				typeof_kz(ymd_type(arg0)));
		return 0;
	}
	return 1;
}

static int libx_exit(L) {
	(void)l;
	longjmp(l->jpt->core, 1); // jump to top
	return 0;
}

// rand()
//     random range no limited
// rand(num)
//     random range to [0, num) num > 0 or (num, 0] num < 0
// rand(min, max)
//     random range [min, max)
static int libx_rand(L) {
	ymd_int_t rv = 0;
	int argc = ymd_argc(l);
	switch (argc) {
	case 0:
		rv = rand();
		break;
	case 1: {
		ymd_int_t limit = int_of(l, ymd_argv(l, 0));
		if (limit > 0)
			rv = rand() % limit;
		else
			rv = -(rand() % (-limit));
		} break;
	case 2:
	default: {
		ymd_int_t arg0 = int_of(l, ymd_argv(l, 0)),
				  arg1 = int_of(l, ymd_argv(l, 1));
		ymd_int_t min, max;
		if (arg0 < arg1)
			min = arg0, max = arg1;
		else if (arg0 > arg1)
			min = arg1, max = arg0;
		else
			rv = arg0;
		if (arg0 != arg1)
			// FIXME: use better random system.
			rv = min + (rand() % (max - min));
		} break;
	}
	ymd_int(l, rv);
	return 1;
}

static const char *gc_state_str[] = {
	"pause",
	"propagate",
	"sweepstring",
	"sweep",
	"finalize",
};

static int libx_gc(L) {
	const struct kstr *arg0 = kstr_of(l, ymd_argv(l, 0));
	if (strcmp(arg0->land, "pause") == 0) {
		gc_active(l->vm, +1);
		return 0;
	} else if (strcmp(arg0->land, "resume") == 0) {
		gc_active(l->vm, -1);
		return 0;
	} else if (strcmp(arg0->land, "step") == 0) {
		gc_step(l->vm);
		return 0;
	} else if (strcmp(arg0->land, "used") == 0) {
		ymd_int(l, l->vm->gc.used);
		return 1;
	} else if (strcmp(arg0->land, "threshold") == 0) {
		ymd_int(l, l->vm->gc.threshold);
		return 1;
	} else if (strcmp(arg0->land, "state") == 0) {
		ymd_kstr(l, gc_state_str[l->vm->gc.state], -1);
		return 1;
	} else if (strcmp(arg0->land, "sweepstep") == 0) {
		ymd_int(l, l->vm->gc.sweep_step);
		return 1;
	}
	return 0;
}

static int libx_setmetatable(L) {
	struct mand *o = mand_of(l, ymd_argv(l, 0));
	if (ymd_type(ymd_argv(l, 1)) != T_HMAP &&
		ymd_type(ymd_argv(l, 1)) != T_SKLS)
		ymd_panic(l, "Not metatable type!");
	mand_proto(o, ymd_argv(l, 1)->u.ref);
	return 0;
}

static int libx_metatable(L) {
	struct mand *o = mand_of(l, ymd_argv(l, 0));
	if (mand_proto(o, NULL))
		setv_ref(ymd_push(l), mand_proto(o, NULL));
	else
		setv_nil(ymd_push(l));
	return 1;
}


static int libx_error(L) {
	int i, argc = ymd_argc(l);
	if (argc > 0) {
		struct zostream os = ZOS_INIT;
		for (i = 0; i < argc; ++i) {
			if (i) zos_append(&os, " ", 1);
			tostring(&os, ymd_argv(l, i));
		}
		ymd_kstr(l, zos_buf(&os), os.last);
		zos_final(&os);
	} else
		ymd_kstr(l, "Unknown", -1);
	ymd_error(l, NULL);
	ymd_raise(l);
	assert(!"No reached.");
	return 0; // Never
}

static int libx_pcall(L) {
	int i, argc = ymd_argc(l);
	struct func *fn = func_of(l, ymd_argv(l, 0));
	setv_func(ymd_push(l), fn);
	for (i = 1; i < argc; ++i)
		*ymd_push(l) = *ymd_argv(l, i);
	i = ymd_pcall(l, fn, argc - 1);
	ymd_skls(l, SKLS_ASC);
	if (i < 0) {
		ymd_move(l, 1);
		ymd_def(l, "backtrace");
		ymd_move(l, 1);
		ymd_def(l, "where");
		ymd_move(l, 1);
		ymd_def(l, "error");
		ymd_int(l, -i);
		ymd_def(l, "level");
	} else {
		if (i) {
			ymd_move(l, 1);
			if (is_nil(ymd_top(l, 0)))
				ymd_pop(l, 1);
			else
				ymd_def(l, "ret");
		}
	}
	return 1;
}

LIBC_BEGIN(Builtin)
	LIBC_ENTRY(print)
	LIBC_ENTRY(insert)
	LIBC_ENTRY(append)
	LIBC_ENTRY(remove)
	LIBC_ENTRY(len)
	LIBC_ENTRY(range)
	LIBC_ENTRY(values)
	LIBC_ENTRY(pairs)
	LIBC_ENTRY(keys)
	LIBC_ENTRY(str)
	LIBC_ENTRY(atoi)
	LIBC_ENTRY(atof)
	LIBC_ENTRY(slice)
	LIBC_ENTRY(split)
	LIBC_ENTRY(panic)
	LIBC_ENTRY(strbuf)
	LIBC_ENTRY(open)
	LIBC_ENTRY(pattern)
	LIBC_ENTRY(match)
	LIBC_ENTRY(import)
	//LIBC_ENTRY(load)
	LIBC_ENTRY(eval)
	LIBC_ENTRY(compile)
	LIBC_ENTRY(env)
	LIBC_ENTRY(exit)
	LIBC_ENTRY(rand)
	LIBC_ENTRY(gc)
	LIBC_ENTRY(setmetatable)
	LIBC_ENTRY(metatable)
	LIBC_ENTRY(error)
	LIBC_ENTRY(pcall)
LIBC_END

int ymd_load_lib(struct ymd_mach *vm, ymd_libc_t lbx) {
	const struct libfn_entry *i;
	struct ymd_context *l = ioslate(vm);
	int rv = 0;
	for (i = lbx; i->native != NULL; ++i) {
		ymd_nafn(l, i->native, i->symbol.z, 0);
		ymd_putg(l, i->symbol.z);
		++rv;
	}
	return rv;
}

int ymd_load_mem(L, const char *clazz, ymd_libc_t lbx) {
	int rv = 0;
	const struct libfn_entry *i;
	for (i = lbx; i->native != NULL; ++i) {
		char name[128];
		strncpy(name, clazz, sizeof(name));
		strcat(name, ".");
		strcat(name, i->symbol.z);
		ymd_kstr(l, i->symbol.z, i->symbol.len);
		ymd_nafn(l, i->native, name, 0);
		ymd_putf(l);
		++rv;
	}
	return rv;
}

