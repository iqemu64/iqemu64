/* -----------------------------------------------------------------------
   ffi_common.h - Copyright (C) 2011, 2012, 2013  Anthony Green
                  Copyright (C) 2007  Free Software Foundation, Inc
                  Copyright (c) 1996  Red Hat, Inc.
                  Copyright (c) 2020 上海芯竹科技有限公司
                  
   Common internal definitions and macros. Only necessary for building
   libffi.
   ----------------------------------------------------------------------- */

//===- ffi commom -------------------------------------------------*- C -*-===//

#ifndef TE_LIBFFI_COMMON_H
#define TE_LIBFFI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "te_config.h"

/* Do not move this. Some versions of AIX are very picky about where
   this is positioned. */
#ifdef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
  /* mingw64 defines this already in malloc.h. */
#  ifndef alloca
#    define alloca __builtin_alloca
#  endif
# endif
# define MAYBE_UNUSED __attribute__((__unused__))
#else
# define MAYBE_UNUSED
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
#   pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
#    ifdef _MSC_VER
#     define alloca _alloca
#    else
char *alloca ();
#   endif
#  endif
# endif
# endif
#endif


/* Check for the existence of memcpy. */
#if STDC_HEADERS
# include <string.h>
#else
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
# endif
#endif


/* v cast to size_t and aligned up to a multiple of a */
#define FFI_ALIGN(v, a)  (((((size_t) (v))-1) | ((a)-1))+1)
/* v cast to size_t and aligned down to a multiple of a */
#define FFI_ALIGN_DOWN(v, a) (((size_t) (v)) & -a)

/* Terse sized type definitions.  */
#if defined(_MSC_VER) || defined(__sgi) || defined(__SUNPRO_C)
typedef unsigned char UINT8;
typedef signed char   SINT8;
typedef unsigned short UINT16;
typedef signed short   SINT16;
typedef unsigned int UINT32;
typedef signed int   SINT32;
# ifdef _MSC_VER
typedef unsigned __int64 UINT64;
typedef signed __int64   SINT64;
# else
# include <inttypes.h>
typedef uint64_t UINT64;
typedef int64_t  SINT64;
# endif
#else
typedef unsigned int UINT8  __attribute__((__mode__(__QI__)));
typedef signed int   SINT8  __attribute__((__mode__(__QI__)));
typedef unsigned int UINT16 __attribute__((__mode__(__HI__)));
typedef signed int   SINT16 __attribute__((__mode__(__HI__)));
typedef unsigned int UINT32 __attribute__((__mode__(__SI__)));
typedef signed int   SINT32 __attribute__((__mode__(__SI__)));
typedef unsigned int UINT64 __attribute__((__mode__(__DI__)));
typedef signed int   SINT64 __attribute__((__mode__(__DI__)));
#endif

typedef float FLOAT32;

#ifndef __GNUC__
#define __builtin_expect(x, expected_value) (x)
#endif
#define LIKELY(x)    __builtin_expect(!!(x),1)
#define UNLIKELY(x)  __builtin_expect((x)!=0,0)

#ifdef __cplusplus
}
#endif

#endif // TE_LIBFFI_COMMON_H
