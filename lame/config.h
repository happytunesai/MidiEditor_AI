/* config.h for LAME — generated for MSVC/CMake builds in MidiEditor_AI */
#ifndef LAME_CONFIG_H
#define LAME_CONFIG_H

#define SIZEOF_DOUBLE 8
#define SIZEOF_FLOAT 4
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_DOUBLE 8
#define SIZEOF_SHORT 2
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_SHORT 2

#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STRING_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1

#define PACKAGE "lame"
#define VERSION "3.100"
#define PROTOTYPES 1
#define USE_FAST_LOG 1

/* Use the LAME library internally (not as DLL) */
#define LAME_LIBRARY_BUILD 1

/* IEEE754 types */
typedef long double ieee854_float80_t;
typedef double      ieee754_float64_t;
typedef float       ieee754_float32_t;

/* Enable SSE on x64 */
#if defined(_M_X64) || defined(__x86_64__)
#define HAVE_XMMINTRIN_H 1
#endif

#endif /* LAME_CONFIG_H */
