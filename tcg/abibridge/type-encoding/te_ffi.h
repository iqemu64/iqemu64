/* -----------------------------------------------------------------*-C-*-
   libffi @VERSION@ - Copyright (c) 2011, 2014, 2019 Anthony Green
                    - Copyright (c) 1996-2003, 2007, 2008 Red Hat, Inc.
                    - Copyright (c) 2020 上海芯竹科技有限公司

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the ``Software''), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   ----------------------------------------------------------------------- */


//===- ffi header for type encoding -------------------------------*- C -*-===//

// from libffi source code, specified for x86_64 and arm64

#ifndef TE_LIBFFI_H
#define TE_LIBFFI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Specify which architecture libffi is configured for. */
#ifndef X86_64
#define X86_64
#endif

#ifndef AARCH64
#define AARCH64
#endif

/* ---- System configuration information --------------------------------- */
#include "tcg/ABIArgInfo.h"
#include "te_ffitarget.h"

#ifndef LIBFFI_ASM

#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(X)
#endif

#include <limits.h>
#include <stddef.h>

/* LONG_LONG_MAX is not always defined (not if STRICT_ANSI, for example).
   But we can find it either under the correct ANSI name, or under GNU
   C's internal name.  */

#define FFI_64_BIT_MAX 9223372036854775807

#ifdef LONG_LONG_MAX
#define FFI_LONG_LONG_MAX LONG_LONG_MAX
#else
#ifdef LLONG_MAX
#define FFI_LONG_LONG_MAX LLONG_MAX
#ifdef _AIX52 /* or newer has C99 LLONG_MAX */
#undef FFI_64_BIT_MAX
#define FFI_64_BIT_MAX 9223372036854775807LL
#endif /* _AIX52 or newer */
#else
#ifdef __GNUC__
#define FFI_LONG_LONG_MAX __LONG_LONG_MAX__
#endif
#ifdef _AIX /* AIX 5.1 and earlier have LONGLONG_MAX */
#ifndef __PPC64__
#if defined(__IBMC__) || defined(__IBMCPP__)
#define FFI_LONG_LONG_MAX LONGLONG_MAX
#endif
#endif /* __PPC64__ */
#undef FFI_64_BIT_MAX
#define FFI_64_BIT_MAX 9223372036854775807LL
#endif
#endif
#endif

/* The closure code assumes that this works on pointers, i.e. a size_t
   can hold a pointer.  */

typedef struct _ffi_type {
  size_t size;
  unsigned short alignment;
  unsigned short type;
  struct _ffi_type **elements;
} ffi_type;

/* The externally visible type declarations also need the MSVC DLL
   decorations, or they will not be exported from the object file.  */
#if defined LIBFFI_HIDE_BASIC_TYPES
#define FFI_EXTERN
#else
#define FFI_EXTERN extern
#endif

#ifndef LIBFFI_HIDE_BASIC_TYPES
#if SCHAR_MAX == 127
#define ffi_type_uchar ffi_type_uint8
#define ffi_type_schar ffi_type_sint8
#else
#error "char size not supported"
#endif

#if SHRT_MAX == 32767
#define ffi_type_ushort ffi_type_uint16
#define ffi_type_sshort ffi_type_sint16
#elif SHRT_MAX == 2147483647
#define ffi_type_ushort ffi_type_uint32
#define ffi_type_sshort ffi_type_sint32
#else
#error "short size not supported"
#endif

#if INT_MAX == 32767
#define ffi_type_uint ffi_type_uint16
#define ffi_type_sint ffi_type_sint16
#elif INT_MAX == 2147483647
#define ffi_type_uint ffi_type_uint32
#define ffi_type_sint ffi_type_sint32
#elif INT_MAX == 9223372036854775807
#define ffi_type_uint ffi_type_uint64
#define ffi_type_sint ffi_type_sint64
#else
#error "int size not supported"
#endif

#if LONG_MAX == 2147483647
#if FFI_LONG_LONG_MAX != FFI_64_BIT_MAX
#error "no 64-bit data type supported"
#endif
#elif LONG_MAX != FFI_64_BIT_MAX
#error "long size not supported"
#endif

#if LONG_MAX == 2147483647
#define ffi_type_ulong ffi_type_uint32
#define ffi_type_slong ffi_type_sint32
#elif LONG_MAX == FFI_64_BIT_MAX
#define ffi_type_ulong ffi_type_uint64
#define ffi_type_slong ffi_type_sint64
#else
#error "long size not supported"
#endif

/* These are defined in types.c.  */
FFI_EXTERN ffi_type ffi_type_void;
FFI_EXTERN ffi_type ffi_type_uint8;
FFI_EXTERN ffi_type ffi_type_sint8;
FFI_EXTERN ffi_type ffi_type_uint16;
FFI_EXTERN ffi_type ffi_type_sint16;
FFI_EXTERN ffi_type ffi_type_uint32;
FFI_EXTERN ffi_type ffi_type_sint32;
FFI_EXTERN ffi_type ffi_type_uint64;
FFI_EXTERN ffi_type ffi_type_sint64;
FFI_EXTERN ffi_type ffi_type_float;
FFI_EXTERN ffi_type ffi_type_double;
FFI_EXTERN ffi_type ffi_type_pointer;

#endif /* LIBFFI_HIDE_BASIC_TYPES */

typedef enum { FFI_OK = 0, FFI_BAD_TYPEDEF, FFI_BAD_ABI } ffi_status;

typedef struct {
  aa_fi_abi abi;
  unsigned nargs;
  ffi_type **arg_types;
  ffi_type *rtype;
  unsigned bytes;
  unsigned flags;
#ifdef AA_FI_EXTRA_CIF_FIELDS
  AA_FI_EXTRA_CIF_FIELDS;
#endif
} aa_fi_cif;

typedef struct {
  xx_fi_abi abi;
  unsigned nargs;
  ffi_type **arg_types;
  ffi_type *rtype;
  // call frame
  unsigned bytes;
  unsigned flags;
} xx_fi_cif;

/* ---- Definitions for the raw API -------------------------------------- */

#define FFI_SIZEOF_ARG 8
// omit java raw API

/* ---- Definitions for closures ----------------------------------------- */

#if FFI_CLOSURES
// omit
#endif /* FFI_CLOSURES */

#if FFI_GO_CLOSURES
// omit
#endif /* FFI_GO_CLOSURES */

/* ---- Public interface definition -------------------------------------- */

ffi_status my_aa_fi_prep_cif(aa_fi_cif *cif, aa_fi_abi abi, unsigned int nargs,
                             ffi_type *rtype, ffi_type **atypes,
                             ABIFnInfo *fnInfo);

ffi_status my_aa_fi_prep_cif_var(aa_fi_cif *cif, aa_fi_abi abi, unsigned nfixed,
                             unsigned ntotal,
                             ffi_type *rtype, ffi_type **atypes,
                             ABIFnInfo *fnInfo);

ffi_status my_xx_fi_prep_cif(xx_fi_cif *cif, xx_fi_abi abi, unsigned int nargs,
                             ffi_type *rtype, ffi_type **atypes,
                             ABIFnInfo *fnInfo);

/* Useful for eliminating compiler warnings.  */
#define FFI_FN(f) ((void (*)(void))f)

/* ---- Definitions shared with assembly code ---------------------------- */

#endif // LIBFFI_ASM

#define FFI_TYPE_VOID 0
#define FFI_TYPE_INT 1
#define FFI_TYPE_FLOAT 2
#define FFI_TYPE_DOUBLE 3
#if 0
#define FFI_TYPE_LONGDOUBLE 4
#else
// x86_64 and arm64 have different definitions on `long double` type.
// but Objective-C does not support that type.  So be it. 
#define FFI_TYPE_LONGDOUBLE FFI_TYPE_DOUBLE
#endif
#define FFI_TYPE_UINT8 5
#define FFI_TYPE_SINT8 6
#define FFI_TYPE_UINT16 7
#define FFI_TYPE_SINT16 8
#define FFI_TYPE_UINT32 9
#define FFI_TYPE_SINT32 10
#define FFI_TYPE_UINT64 11
#define FFI_TYPE_SINT64 12
#define FFI_TYPE_STRUCT 13
#define FFI_TYPE_POINTER 14
#define FFI_TYPE_COMPLEX 15

/* This should always refer to the last type code (for sanity checks).  */
#define FFI_TYPE_LAST FFI_TYPE_COMPLEX

#ifdef __cplusplus
}
#endif

#endif // TE_LIBFFI_H
