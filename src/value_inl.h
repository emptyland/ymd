#ifndef YMD_VALUE_INL_H
#define YMD_VALUE_INL_H

//
// Type getter
//
static inline int ymd_type(const struct variable *var) {
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
#define is_num(v) ((v)->tt == T_INT || (v)->tt == T_FLOAT)

//
// Variable setter:
//
static inline void setv_nil (struct variable *v) {
	v->tt = T_NIL; v->u.i = 0;
}
#define SETV_DECL(name, arg1) \
	static inline void setv_##name (struct variable *v, arg1 x)
SETV_DECL(int, ymd_int_t) {
	v->tt = T_INT; v->u.i = x;
}
SETV_DECL(float, ymd_float_t) {
	v->tt = T_FLOAT; v->u.f = x;
}
SETV_DECL(bool, ymd_int_t) {
	v->tt = T_BOOL; v->u.i = x;
}
SETV_DECL(ext, void *) {
	v->tt = T_EXT; v->u.ext = x;
}
SETV_DECL(ref, struct gc_node *) {
	v->tt = T_REF; v->u.ref = gcx(x);
}
#undef SETV_DECL
#define DEFINE_SETTER(name, ty) \
static inline void setv_##name(struct variable *v, struct name *o) { \
	v->tt = T_REF; \
	v->u.ref = gcx(o); \
}
DECL_TREF(DEFINE_SETTER)
#undef DEFINE_SETTER

// Check lhs and rhs one of float type
static inline int floatize(const struct variable *lhs,
		const struct variable *rhs) {
	return ymd_type(lhs) == T_FLOAT || ymd_type(rhs) == T_FLOAT;
}

static inline
struct gc_node *mand_proto(struct mand *x, struct gc_node *metatable) {
	if (!metatable) return x->proto;
	assert(metatable->type == T_HMAP || metatable->type == T_SKLS);
	x->proto = metatable;
	return NULL;
}

// Number of function's local variable.
static inline int func_nlocal(const struct func *fn) {
	return fn->is_c ? 0 : fn->u.core->klz;
}

// Defined function's first line number.
static inline int func_line(const struct func *fn) {
	assert (!fn->is_c && "C function is not defined by script.");
	assert (fn->u.core->kinst > 0);
	return fn->u.core->line[0];
}

static inline int func_argv(const struct func *fn) {
	return fn->is_c ? 0 : fn->u.core->argv;
}

#endif // YMD_VALUE_INL_H
