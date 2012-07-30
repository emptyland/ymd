#include "tostring.h"
#include "value.h"
#include "value_inl.h"
#include <stdlib.h>

void fmtx_final(struct fmtx *self) {
	if (self->dy) free(self->dy);
	memset(self->kbuf, 0, sizeof(self->kbuf));
	self->dy = NULL;
	self->last = 0;
	self->max  = FMTX_STATIC_MAX;
}

void fmtx_need(struct fmtx *self, int n) {
	if (self->last + n <= (int)sizeof(self->kbuf))
		return;
	if (self->last + n <= self->max)
		return;
	self->max = (self->last + n) * 3 / 2 + FMTX_STATIC_MAX;
	if (!self->dy) {
		self->dy = calloc(self->max, 1);
		memcpy(self->dy, self->kbuf, self->last);
	} else {
		self->dy = realloc(self->dy, self->max);
	}
}

const char *tostring(struct fmtx *ctx, const struct variable *var) {
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
