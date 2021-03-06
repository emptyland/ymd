#ifndef YMD_BUILTIN_H
#define YMD_BUILTIN_H

struct ymd_context;
struct ymd_mach;
struct variable;
struct chunk;
struct func;
struct kstr;
struct dyay;
struct kvi;
struct hmap;
struct sknd;
struct skls;

typedef long long          ymd_int_t;
typedef unsigned long long ymd_uint_t;
typedef double             ymd_float_t;

typedef int (*ymd_nafn_t)(struct ymd_context *);

typedef unsigned char      ymd_byte_t;
typedef int                ymd_i32_t;
typedef unsigned int       ymd_u32_t;

typedef int (*ymd_final_t)(void *);

// Byte code types:
typedef unsigned int       ymd_inst_t;
typedef unsigned int       uint_t;
typedef unsigned char      uchar_t;
typedef unsigned short     ushort_t;

#define MAX_U16 0xFFFF
#define MAX_U32 0xFFFFFFFF

#define ARRAY_SIZEOF(arr) (sizeof(arr)/sizeof(arr[0]))

#define YMD_MIN(lhs, rhs) (((lhs) < (rhs)) ? (lhs) : (rhs))
#define YMD_MAX(lhs, rhs) (((lhs) > (rhs)) ? (lhs) : (rhs))

#if defined(_MSC_VER)
#	define YMD_INLINE   __inline
#	define YMD_READ_MOD "rb"
#	define YMD_NORETURN __declspec(noreturn)

#	define snprintf     _snprintf
#else
#	define YMD_INLINE inline
#	define YMD_READ_MOD "r"
#	define YMD_NORETURN
#endif

#endif // YMD_BUILTIN_H
