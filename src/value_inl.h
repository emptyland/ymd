#ifndef YMD_VALUE_INL_H
#define YMD_VALUE_INL_H

//
// Type getter
//
static inline int TYPEV(const struct variable *var) {
	return (var)->tt == T_REF ? (var)->u.ref->type : (var)->tt;
}

//
// Get referenced object for variable
//
#define DEFINE_REFCAST(name, ty) \
static inline struct name *name##_x(struct variable *var) { \
	assert(var->u.ref->type == ty); \
	return (struct name *)var->u.ref; \
} \
static inline const struct name *name##_k(const struct variable *var) { \
	assert(var->u.ref->type == ty); \
	return (const struct name *)var->u.ref; \
} \
static inline struct name *name##_f(void *o) { \
	return (struct name *)o; \
}
DECL_TREF(DEFINE_REFCAST)
#undef DEFINE_REFCAST

#define DECL_REFOF(name, ty) \
	struct name *name##_of(struct ymd_context *l, struct variable *var);
DECL_TREF(DECL_REFOF)
#undef DECL_REFOF

#define mand_cast(var, ty, type) ((type)mand_land(var, ty))

#define is_nil(v) ((v)->tt == T_NIL)
#define is_ref(v) ((v)->tt == T_REF)

//
// Variable setter:
//
static inline void setv_nil (struct variable *v) {
	v->tt = T_NIL; v->u.i = 0;
}
#define VSET_DECL(name, arg1) \
	static inline void setv_##name (struct variable *v, arg1 x)
VSET_DECL(int, ymd_int_t) {
	v->tt = T_INT; v->u.i = x;
}
VSET_DECL(bool, ymd_int_t) {
	v->tt = T_BOOL; v->u.i = x;
}
VSET_DECL(ext, void *) {
	v->tt = T_EXT; v->u.ext = x;
}
VSET_DECL(ref, struct gc_node *) {
	v->tt = T_REF; v->u.ref = gcx(x);
}
#undef VSET_DECL
#define DEFINE_SETTER(name, ty) \
static inline void setv_##name(struct variable *v, struct name *o) { \
	v->tt = T_REF; \
	v->u.ref = gcx(o); \
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
	return fn->is_c ? 0 : fn->u.core->klz;
}

#endif // YMD_VALUE_INL_H
