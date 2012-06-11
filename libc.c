#include "state.h"
#include "value.h"
#include "memory.h"
#include "libc.h"
#include <stdio.h>

#define PRINT_SPLIT " "

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
	default:
		assert(0);
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

static int libx_add(struct context *l) {
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
	const struct dyay *argv = ymd_argv(l);
	const struct variable *arg0;
	if (!argv)
		vm_die("Void argument, need 1");
	arg0 = argv->elem + 0;
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

LIBC_BEGIN(Builtin)
	LIBC_ENTRY(print)
	LIBC_ENTRY(add)
	LIBC_ENTRY(len)
LIBC_END


int ymd_load_lib(ymd_libc_t lbx) {
	const struct libfn_entry *i;
	for (i = lbx; i->native != NULL; ++i) {
		struct kstr *kz = ymd_kstr(i->symbol.z, i->symbol.len);
		struct func *fn = func_new(i->native);
		struct variable key, *rv;
		key.type = T_KSTR;
		key.value.ref = (struct gc_node *)kz;
		rv = hmap_get(vm()->global, &key);
		rv->type = T_FUNC;
		func_proto(fn);
		rv->value.ref = (struct gc_node *)fn;
	}
	return 0;
}
