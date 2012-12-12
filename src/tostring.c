#include "tostring.h"
#include "value.h"
#include "value_inl.h"
#include <stdlib.h>

#define zos_add(os) zos_advance(os, (int)strlen(zos_last(os)))

static const char *dyay_tostring(struct zostream *os, const struct dyay *o) {
	int i;
	if (fg_self(o))
		return zos_append(os, "..[self]..", 10);
	zos_append(os, "[", 1);
	fg_enter(gcx(o));
	for (i = 0; i < o->count; ++i) {
		if (i > 0) zos_append(os, ", ", 2);
		tostring(os, o->elem + i);
	}
	fg_leave(gcx(o));
	return zos_append(os, "]", 1);
}

static const char *hmap_tostring(struct zostream *os, const struct hmap *o) {
	struct kvi *initial = o->item, *i = NULL,
			   *k = initial + (1 << o->shift);
	int f = 0;
	if (fg_self(o))
		return zos_append(os, "..{self}..", 10);
	zos_append(os, "{", 1);
	fg_enter(gcx(o));
	for (i = initial; i != k; ++i) {
		if (!i->flag) continue;
		if (f++ > 0) zos_append(os, ", ", 2);
		tostring(os, &i->k);
		zos_append(os, " : ", 3);
		tostring(os, &i->v);
	}
	fg_leave(gcx(o));
	return zos_append(os, "}", 1);
}

static const char *skls_tostring(struct zostream *os, const struct skls *o) {
	struct sknd *initial = o->head->fwd[0], *i = NULL;
	int f = 0;
	if (fg_self(o))
		return zos_append(os, "..@{self}..", 11);
	zos_append(os, "@{", 2);
	fg_enter(gcx(o));
	for (i = initial; i != NULL; i = i->fwd[0]) {
		if (f++ > 0) zos_append(os, ", ", 2);
		tostring(os, &i->k);
		zos_append(os, " : ", 3);
		tostring(os, &i->v);
	}
	fg_leave(gcx(o));
	return zos_append(os, "}", 1);
}

static const char *mand_tostring(struct zostream *os, const struct mand *o) {
	zos_append(os, "(", 1);
	if (o->tt)
		zos_append(os, o->tt, strlen(o->tt));
	else
		zos_append(os, "*", 1);
	zos_append(os, ")", 1);
	zos_reserved(os, 10 + 24 + 2);
	snprintf(zos_last(os), zos_remain(os), "[%d@%p]", o->len, o->land);
	zos_add(os);
	return zos_buf(os);
}

const char *tostring(struct zostream *os, const struct variable *var) {
	switch (TYPEV(var)) {
	case T_NIL:
		zos_append(os, "nil", 3);
		break;
	case T_INT:
		zos_reserved(os, 24);
		snprintf(zos_last(os), zos_remain(os), "%lld", var->u.i);
		zos_add(os);
		break;
	case T_BOOL:
		if (var->u.i)
			zos_append(os, "true", 4);
		else
			zos_append(os, "false", 5);
		break;
	case T_EXT:
		zos_reserved(os, 24);
		snprintf(zos_last(os), zos_remain(os), "@%p", var->u.ext);
		zos_add(os);
		break;
	case T_KSTR:
		zos_append(os, kstr_k(var)->land, kstr_k(var)->len);
		break;
	case T_FUNC:
		zos_append(os, func_k(var)->proto->land, func_k(var)->proto->len);
		break;
	case T_DYAY:
		dyay_tostring(os, dyay_k(var));
		break;
	case T_HMAP:
		hmap_tostring(os, hmap_k(var));
		break;
	case T_SKLS:
		skls_tostring(os, skls_k(var));
		break;
	case T_MAND:
		mand_tostring(os, mand_k(var));
		break;
	default:
		assert(!"No reached");
		return NULL;
	}
	return zos_buf(os);
}
