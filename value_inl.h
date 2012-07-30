#ifndef YMD_VALUE_INL_H
#define YMD_VALUE_INL_H

#define DEFINE_REFCAST(name, tt) \
static inline struct name *name##_x(struct variable *var) { \
	assert(var->value.ref->type == tt); \
	return (struct name *)var->value.ref; \
} \
static inline const struct name *name##_k(const struct variable *var) { \
	assert(var->value.ref->type == tt); \
	return (const struct name *)var->value.ref; \
} \
static inline struct name *name##_f(void *o) { \
	return (struct name *)o; \
}
DECL_TREF(DEFINE_REFCAST)
#undef DEFINE_REFCAST

#define DECL_REFOF(name, tt) \
struct name *name##_of(struct ymd_context *l, struct variable *var);
DECL_TREF(DECL_REFOF)
#undef DECL_REFOF

#define mand_cast(var, tt, type) ((type)mand_land(var, tt))

#define is_nil(v) ((v)->type == T_NIL)
#define is_ref(v) ((v)->type >= T_REF)

/*
#define vset_nil(v) \
	{ (v)->type = T_NIL; (v)->value.i = 0; }
#define vset_int(v, x) \
	{ (v)->type = T_INT; (v)->value.i = (x); }
#define vset_bool(v, x) \
	{ (v)->type = T_BOOL; (v)->value.i = (x); }
#define vset_ext(v, x) \
	{ (v)->type = T_EXT; (v)->value.ext = (x); }
#define vset_ref(v, x) \
	{ (v)->type = gcx(x)->type; (v)->value.ref = gcx(x); }
*/
static inline void vset_nil (struct variable *v) {
	v->type = T_NIL; v->value.i = 0;
}
#define VSET_DECL(name, arg1) \
	static inline void vset_##name (struct variable *v, arg1 x)
VSET_DECL(int, ymd_int_t) {
	v->type = T_INT; v->value.i = x;
}
VSET_DECL(bool, ymd_int_t) {
	v->type = T_BOOL; v->value.i = x;
}
VSET_DECL(ext, void *) {
	v->type = T_EXT; v->value.ext = x;
}
VSET_DECL(ref, struct gc_node *) {
	v->type = gcx(x)->type; v->value.ref = gcx(x);
}
#undef VSET_DECL
#define DEFINE_SETTER(name, tt) \
static inline void vset_##name(struct variable *v, struct name *o) { \
	v->type = tt; \
	v->value.ref = gcx(o); \
}
DECL_TREF(DEFINE_SETTER)
#undef DEFINE_SETTER

static inline
struct gc_node *mand_proto(struct mand *x, struct gc_node *metatable) {
	if (!metatable) return x->proto;
	assert(metatable->type == T_HMAP || metatable->type == T_SKLS);
	x->proto = metatable;
	return NULL;
}

static inline int func_nlocal(const struct func *fn) {
	return fn->is_c ? 0 : fn->u.core->klz - fn->n_bind;
}

static inline struct variable *func_bval(struct ymd_mach *vm,
                                         struct func *fn, int i) {
	struct variable nil; memset(&nil, 0, sizeof(nil));
	func_bind(vm, fn, i, &nil);
	return fn->bind + i;
}

#endif // YMD_VALUE_INL_H
