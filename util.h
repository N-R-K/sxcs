#include <limits.h>
#include <stddef.h>

/* Typedefs */
typedef unsigned int     uint;
typedef unsigned short   ushort;
typedef unsigned long    ulong;
typedef unsigned char    uchar;

/* fixed bit types */
#define __FIXED_BIT_TYPE_ERROR do { struct { int a[-1]; } fixed_bit_not_available; } while(0);
#if !defined(CHAR_BIT) || !defined(USHRT_MAX) || !defined(UINT_MAX) || !defined(ULONG_MAX)
	#error "is <limits.h> included?"
#endif

#if CHAR_BIT == 8
	typedef signed   char    i8;
	typedef unsigned char    u8;
	#define FMT_I8 "hhd"
	#define FMT_U8 "hhu"
#else
	#define i8 __FIXED_BIT_TYPE_ERROR
	#define u8 __FIXED_BIT_TYPE_ERROR
#endif

#if USHRT_MAX == 65535UL
	typedef signed   short   i16;
	typedef unsigned short   u16;
	#define FMT_I16 "hd"
	#define FMT_U16 "hu"
#else
	#define i16 __FIXED_BIT_TYPE_ERROR
	#define u16 __FIXED_BIT_TYPE_ERROR
#endif

#if UINT_MAX == 4294967295UL
	typedef signed   int     i32;
	typedef unsigned int     u32;
	#define FMT_I32 "d"
	#define FMT_U32 "u"
#elif ULONG_MAX == 4294967295UL
	typedef signed   long    i32;
	typedef unsigned long    u32;
	#define FMT_I32 "ld"
	#define FMT_U32 "lu"
#else
	#define i32 __FIXED_BIT_TYPE_ERROR
	#define u32 __FIXED_BIT_TYPE_ERROR
#endif

#if ULONG_MAX > 4294967295UL
	/* safe to shift right by 32 */
	#if ((ULONG_MAX >> 32) >> 31) == 1
		typedef signed   long    i64;
		typedef unsigned long    u64;
		#define FMT_I64 "ld"
		#define FMT_U64 "lu"
	#else
		#define i64 __FIXED_BIT_TYPE_ERROR
		#define u64 __FIXED_BIT_TYPE_ERROR
	#endif
#else
	#define i64 __FIXED_BIT_TYPE_ERROR
	#define u64 __FIXED_BIT_TYPE_ERROR
#endif

/* compiler attributes and builtins */
#ifndef __has_attribute
	#define __has_attribute(X) (0)
#endif

#ifndef __has_builtin
	#define __has_builtin(X) (0)
#endif

#if __has_attribute(pure)
	#define ATTR_PURE __attribute__ ((pure))
#else
	#define ATTR_PURE
#endif

/* uses ATTR_PURE as fallback */
#if __has_attribute(const)
	#define ATTR_CONST __attribute__ ((const))
#else
	#define ATTR_CONST ATTR_PURE
#endif

#if __has_attribute(returns_nonnull)
	#define ATTR_RET_NON_NULL __attribute__ ((returns_nonnull))
#else
	#define ATTR_RET_NON_NULL
#endif

#if __has_attribute(malloc)
	#define ATTR_MALLOC __attribute__ ((malloc))
#else
	#define ATTR_MALLOC
#endif

#if __has_attribute(hot)
	#define ATTR_HOT __attribute__ ((hot))
#else
	#define ATTR_HOT
#endif

#if __has_attribute(cold)
	#define ATTR_COLD __attribute__ ((cold))
#else
	#define ATTR_COLD
#endif

#if __has_attribute(unused)
	#define ATTR_UNUSED __attribute__ ((unused))
#else
	#define ATTR_UNUSED
#endif

#if __has_builtin(__builtin_expect)
	#define likely(expr) __builtin_expect(!!(expr), 1)
	#define unlikely(expr) __builtin_expect(!!(expr), 0)
#else
	#define likely(expr) (expr)
	#define unlikely(expr) (expr)
#endif

/* Macros */
#define ARRLEN(X)        (sizeof(X) / sizeof((X)[0]))
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#define ABS(X)           (((X) > 0) ? (X) : -(X))
#define DIFF(A, B)       ((A) > (B) ? (A) - (B) : (B) - (A))
#define UNUSED(X)        ((void)(X)) /* silence compiler warnings */
/* 0 = least significant bit */
#define BIT_SET(var, bit, val) (var = val ? var | (1 << bit) : var & ~(1 << bit))
#define BIT_TOGGLE(var, bit)   (var = (var & (1 << bit)) ? var & ~(1 << bit) : var | (1 << bit))

/* function declarations */
extern void error(int exit_status, int errnum, const char *fmt, ...);
extern void * emalloc(size_t size) ATTR_MALLOC ATTR_RET_NON_NULL;
extern void * ecalloc(size_t nmemb, size_t size) ATTR_MALLOC ATTR_RET_NON_NULL;
extern void * erealloc(void *ptr, size_t size) ATTR_RET_NON_NULL;
