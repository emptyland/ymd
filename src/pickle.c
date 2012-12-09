#include "pickle.h"
#include "core.h"
#include "encoding.h"
#include "zstream.h"

static inline void if_recursived(const void *p, int *ok) {
	const struct gc_node *o = p; *ok = !fg_self(o);
}
#define mutable(obj) ((struct gc_node *)(obj))

#define CHECK_OK ok); if (!*ok) return 0; ((void)0
#define pickle_assert(expr) if (!(expr)) { *ok = 0; return 0; } (void)0

int ymd_dump_kstr(struct zostream *os, const struct kstr *kz) {
	int i = 0;
	// :tt
	i += zos_u32(os, T_KSTR);
	// :len
	i += zos_u32(os, kz->len);
	// :land
	zos_append(os, kz->land, kz->len);
	i += kz->len;
	return i;
}

int ymd_dump_dyay(struct zostream *os, const struct dyay *ax, int *ok) {
	int j, i = 0;
	if_recursived(ax, CHECK_OK);
	fg_enter(mutable(ax));
	// :tt
	i += zos_u32(os, T_DYAY);
	// :count
	i += zos_u32(os, ax->count);
	// :elem
	for (j = 0; j < ax->count; ++j) {
		i += ymd_serialize(os, ax->elem + j, CHECK_OK);
	}
	fg_leave(mutable(ax));
	return i;
}

int ymd_dump_hmap(struct zostream *os, const struct hmap *mx, int *ok) {
	int n, t = 0;
	const struct kvi *i, *k = mx->item + (1 << mx->shift);
	if_recursived(mx, CHECK_OK);
	fg_enter(mutable(mx));
	// :tt
	t += zos_u32(os, T_HMAP);
	// :count
	for (n = 0, i = mx->item; i != k; ++i) if (i->flag) ++n;
	t += zos_u32(os, n);
	// :item
	for (i = mx->item; i != k; ++i) {
		if (i->flag) {
			t += ymd_serialize(os, &i->k, CHECK_OK);
			t += ymd_serialize(os, &i->v, CHECK_OK);
		}
	}
	fg_leave(mutable(mx));
	return t;
}

int ymd_dump_skls(struct zostream *os, const struct skls *sk, int *ok) {
	int n, t = 0;
	const struct sknd *i;
	if_recursived(sk, CHECK_OK);
	fg_enter(mutable(sk));
	// :tt
	t += zos_u32(os, T_SKLS);
	// :count
	for (n = 0, i = sk->head->fwd[0]; i != NULL; i = i->fwd[0]) ++n;
	t += zos_u32(os, n);
	// :node
	for (i = sk->head->fwd[0]; i != NULL; i = i->fwd[0]) {
		t += ymd_serialize(os, &i->k, CHECK_OK);
		t += ymd_serialize(os, &i->v, CHECK_OK);
	}
	fg_leave(mutable(sk));
	return t;
}

int ymd_dump_chunk(struct zostream *os, const struct chunk *bk, int *ok) {
	int k, j, i = 0;
	// :inst
	i += zos_u32(os, bk->kinst);
	k = (int)sizeof(*bk->inst) * bk->kinst;
	zos_append(os, bk->inst, k);
	i += k;
	for (k = 0, j = 0; j < bk->kinst; ++j)
		zos_u32(os, bk->line[j]);
	// :kval
	zos_u32(os, bk->kkval);
	for (j = 0; j < bk->kkval; ++j) {
		i += ymd_serialize(os, bk->kval + j, CHECK_OK);
	}
	// :lz
	zos_u32(os, bk->klz);
	for (j = 0; j < (int)bk->klz; ++j)
		i += ymd_dump_kstr(os, bk->lz[j]);
	// :file
	i += ymd_dump_kstr(os, bk->file);
	// :kargs
	i += zos_u32(os, bk->kargs);
	return i;
}

int ymd_dump_func(struct zostream *os, const struct func *fn, int *ok) {
	int j, i = 0;
	if (fn->is_c) return 0;
	// :tt
	i += zos_u32(os, T_FUNC);
	// :proto type desc
	i += ymd_dump_kstr(os, fn->proto);
	// :bind variable
	i += zos_u32(os, fn->n_bind);
	for (j = 0; j < fn->n_bind; ++j) {
		i += ymd_serialize(os, fn->bind + j, CHECK_OK);
	}
	// :arguements
	// Don't serialize arguements.
	// :chunk
	i += ymd_dump_chunk(os, fn->u.core, CHECK_OK);
	return i;
}

int ymd_serialize(struct zostream *os, const struct variable *v,
	              int *ok) {
	int i = 0;
	switch (v->type) {
	case T_NIL:
		i += zos_u32(os, T_NIL);
		break;
	case T_INT:
		i += zos_u32(os, T_INT);
		i += zos_i64(os, v->u.i);
		break;
	case T_BOOL:
		i += zos_u32(os, T_BOOL);
		i += zos_u64(os, v->u.i);
		break;
	case T_KSTR:
		i += ymd_dump_kstr(os, kstr_k(v));
		break;
	case T_FUNC:
		i += ymd_dump_func(os, func_k(v), ok);
		break;
	case T_DYAY:
		i += ymd_dump_dyay(os, dyay_k(v), ok);
		break;
	case T_HMAP:
		i += ymd_dump_hmap(os, hmap_k(v), ok);
		break;
	case T_SKLS:
		i += ymd_dump_skls(os, skls_k(v), ok);
		break;
	default:
		*ok = 0;
		break;
	}
	return i;
}

int ymd_pdump(struct ymd_context *l) {
	struct zostream os = ZOS_INIT;
	int i, ok = 1;
	if ((i = ymd_serialize(&os, ymd_top(l, 0), &ok)) > 0 && ok) {
		ymd_pop(l, 1);
		ymd_kstr(l, zos_buf(&os), os.last);
	} else {
		ymd_pop(l, 1);
		ymd_nil(l);
	}
	zos_final(&os);
	return ok ? i : 0;
}


int ymd_load_kstr(struct zistream *is, int *ok) {
	struct ymd_context *l = context(is);
	int len = zis_u32(is);
	pickle_assert(zis_remain(is) >= len);
	ymd_kstr(l, len ? zis_last(is) : "", len);
	zis_advance(is, len);
	return 0;
}

int ymd_load_dyay(struct zistream *is, int *ok) {
	struct ymd_context *l = context(is);
	int i = zis_u32(is);
	(void)ok;
	ymd_dyay(l, i);
	while (i--) {
		ymd_parse(is, CHECK_OK);
		ymd_add(l);
	}
	return 0;
}

int ymd_load_chunk(struct zistream *is, struct chunk *x, int *ok) {
	struct ymd_context *l = context(is);
	int i, k = zis_u32(is);
	pickle_assert(k > 0);
	// :inst
	x->inst = mm_zalloc(l->vm, k, sizeof(*x->inst));
	zis_fetch(is, x->inst, k * sizeof(*x->inst));
	// :line
	x->line = mm_zalloc(l->vm, k, sizeof(*x->line));
	for (i = 0; i < k; ++i)
		x->line[i] = zis_u32(is);
	x->kinst = k;
	// :kval
	k = zis_u32(is);
	if (k > 0) {
		x->kval = mm_zalloc(l->vm, k, sizeof(*x->kval));
		for (i = 0; i < k; ++i) {
			ymd_parse(is, CHECK_OK);
			x->kval[i] = *ymd_top(l, 0);
			ymd_pop(l, 1);
		}
		x->kkval = k;
	}
	// :lz
	k = zis_u32(is);
	if (k > 0) {
		pickle_assert(k < MAX_U16);
		x->lz = mm_zalloc(l->vm, k, sizeof(*x->lz));
		for (i = 0; i < k; ++i) {
			pickle_assert(zis_u32(is) == T_KSTR);
			ymd_load_kstr(is, CHECK_OK);
			x->lz[i] = kstr_of(l, ymd_top(l, 0));
			ymd_pop(l, 1);
		}
		x->klz = k;
	}
	// :file
	pickle_assert(zis_u32(is) == T_KSTR);
	ymd_load_kstr(is, CHECK_OK);
	x->file = kstr_of(l, ymd_top(l, 0));
	ymd_pop(l, 1);
	// :kargs
	x->kargs = zis_u32(is);
	pickle_assert(x->kargs < 16);
	return 0;
}

int ymd_load_func(struct zistream *is, int *ok) {
	struct ymd_context *l = context(is);
	struct chunk *x = mm_zalloc(l->vm, 1, sizeof(*x));
	struct func *fn = ymd_naked(l, x);
	int i;
	// :proto
	pickle_assert(T_KSTR == zis_u32(is));
	ymd_load_kstr(is, CHECK_OK);
	fn->proto = kstr_of(l, ymd_top(l, 0));
	ymd_pop(l, 1);
	// :bind
	fn->n_bind = zis_u32(is);
	for (i = 0; i < fn->n_bind; ++i) {
		ymd_parse(is, CHECK_OK);
		ymd_bind(l, i);
	}
	// :chunk
	i = ymd_load_chunk(is, x, CHECK_OK);
	return i;
}

int ymd_load_hmap(struct zistream *is, int *ok) {
	struct ymd_context *l = context(is);
	int i, k = zis_u32(is);
	ymd_hmap(l, k);
	for (i = 0; i < k; ++i) {
		ymd_parse(is, CHECK_OK);
		ymd_parse(is, CHECK_OK);
		ymd_putf(l);
	}
	return 0;
}

int ymd_load_skls(struct zistream *is, int *ok) {
	struct ymd_context *l = context(is);
	int i, k = zis_u32(is);
	ymd_skls(l);
	for (i = 0; i < k; ++i) {
		ymd_parse(is, CHECK_OK);
		ymd_parse(is, CHECK_OK);
		ymd_putf(l);
	}
	return 0;
}

int ymd_parse(struct zistream *is, int *ok) {
	struct ymd_context *l = context(is);
	ymd_u32_t tt = zis_u32(is);
	int off = is->last;
	switch (tt) {
	case T_NIL:
		ymd_nil(l);
		break;
	case T_INT:
		ymd_int(l, zis_i64(is));
		break;
	case T_BOOL:
		ymd_bool(l, zis_u64(is));
		break;
	case T_KSTR:
		ymd_load_kstr(is, CHECK_OK);
		break;
	case T_FUNC:
		ymd_load_func(is, CHECK_OK);
		break;
	case T_DYAY:
		ymd_load_dyay(is, CHECK_OK);
		break;
	case T_HMAP:
		ymd_load_hmap(is, CHECK_OK);
		break;
	case T_SKLS:
		ymd_load_skls(is, CHECK_OK);
		break;
	default:
		*ok = 0;
		break;
	}
	return is->last - off;
}

int ymd_pload(struct ymd_context *l, const void *z, int k) {
	struct zistream is = ZIS_INIT(l, z, k);
	int i = 0, ok = 1;
	if (ymd_parse(&is, &ok) > 0 && ok)
		i = is.last;
	else
		ymd_pop(l, 1);
	zis_final(&is);
	return i;
}
