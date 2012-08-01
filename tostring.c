#include "tostring.h"
#include "value.h"
#include "value_inl.h"
#include <stdlib.h>

#define zos_add(os) zos_advance(os, (int)strlen(zos_last(os)))

const char *tostring(struct zostream *os, const struct variable *var) {
	switch (var->type) {
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
	case T_DYAY: {
		int i;
		if (fg_self(var->u.ref))
			return zos_append(os, "..[self]..", 10);
		zos_append(os, "[", 1);
		fg_enter(gcx(var->u.ref));
		for (i = 0; i < dyay_k(var)->count; ++i) {
			if (i > 0) zos_append(os, ", ", 2);
			tostring(os, dyay_k(var)->elem + i);
		}
		fg_leave(gcx(var->u.ref));
		zos_append(os, "]", 1);
		} break;
	case T_HMAP: {
		struct kvi *initial = hmap_k(var)->item,
				   *i = NULL,
				   *k = initial + (1 << hmap_k(var)->shift);
		int f = 0;
		if (fg_self(var->u.ref))
			return zos_append(os, "..{self}..", 10);
		zos_append(os, "{", 1);
		fg_enter(gcx(var->u.ref));
		for (i = initial; i != k; ++i) {
			if (!i->flag) continue;
			if (f++ > 0) zos_append(os, ", ", 2);
			tostring(os, &i->k);
			zos_append(os, " : ", 3);
			tostring(os, &i->v);
		}
		fg_leave(gcx(var->u.ref));
		zos_append(os, "}", 1);
		} break;
	case T_SKLS: {
		struct sknd *initial = skls_k(var)->head->fwd[0],
				    *i = NULL;
		int f = 0;
		if (fg_self(var->u.ref))
			return zos_append(os, "..@{self}..", 11);
		zos_append(os, "@{", 2);
		fg_enter(gcx(var->u.ref));
		for (i = initial; i != NULL; i = i->fwd[0]) {
			if (f++ > 0) zos_append(os, ", ", 2);
			tostring(os, &i->k);
			zos_append(os, " : ", 3);
			tostring(os, &i->v);
		}
		fg_leave(gcx(var->u.ref));
		zos_append(os, "}", 1);
		} break;
	case T_MAND: {
		zos_append(os, "(", 1);
		if (mand_k(var)->tt)
			zos_append(os, mand_k(var)->tt, strlen(mand_k(var)->tt));
		else
			zos_append(os, "*", 1);
		zos_append(os, ")", 1);
		zos_reserved(os, 10 + 24 + 2);
		snprintf(zos_last(os), zos_remain(os), "[%d@%p]",
		         mand_k(var)->len, mand_k(var)->land);
		zos_add(os);
		} break;
	default:
		assert(0);
		return NULL;
	}
	return zos_buf(os);
}
