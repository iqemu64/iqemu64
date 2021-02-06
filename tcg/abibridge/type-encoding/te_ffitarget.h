/* Copyright (c) 2009, 2010, 2011, 2012 ARM Ltd.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
``Software''), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */


//===- ffitarget for type encoding --------------------------------*- C -*-===//

#ifndef TE_LIBFFI_TARGET_H
#define TE_LIBFFI_TARGET_H

#ifndef TE_LIBFFI_H
#error "Use te_ffi.h instead."
#endif

/* System specific configurations */

#if defined(X86_64) && defined(__i386__)
#undef X86_64
#define X86
#endif

#define XX_FI_TARGET_SPECIFIC_STACK_SPACE_ALLOCATION
#ifndef _MSC_VER
#define XX_FI_TARGET_HAS_COMPLEX_TYPE
#endif

/* Generic type definitions */

#ifndef LIBFFI_ASM

// inactive macros are removed
typedef unsigned long ffi_arg;
typedef signed long ffi_sarg;

typedef enum aa_fi_abi {
  AA_FI_FIRST_ABI = 0,
  AA_FI_SYSV,
  AA_FI_LAST_ABI,
  AA_FI_DEFAULT_ABI = AA_FI_SYSV
} aa_fi_abi;

typedef enum xx_fi_abi {
  XX_FI_FIRST_ABI = 1,
  XX_FI_UNIX64,
  XX_FI_WIN64,
  XX_FI_EFI64 = XX_FI_WIN64,
  XX_FI_GNUW64,
  XX_FI_LAST_ABI,
  XX_FI_DEFAULT_ABI = XX_FI_UNIX64
} xx_fi_abi;
#endif

/* Definitions for closures */

// omit
#if 0
#define FFI_CLOSURES 1
#define FFI_NATIVE_RAW_API 0

#if defined(FFI_EXEC_TRAMPOLINE_TABLE) && FFI_EXEC_TRAMPOLINE_TABLE

#ifdef __MACH__
#define FFI_TRAMPOLINE_SIZE 16
#define FFI_TRAMPOLINE_CLOSURE_OFFSET 16
#else
#error "No trampoline table implementation"
#endif

#else
#define FFI_TRAMPOLINE_SIZE 24
#define FFI_TRAMPOLINE_CLOSURE_OFFSET FFI_TRAMPOLINE_SIZE
#endif

#ifdef _M_ARM64
#define FFI_EXTRA_CIF_FIELDS unsigned is_variadic
#endif
#endif

/* ---- Internal ---- */

#if defined(__APPLE__)
#define AA_FI_TARGET_SPECIFIC_VARIADIC
#define AA_FI_EXTRA_CIF_FIELDS unsigned aarch64_nfixedargs
#elif !defined(_M_ARM64)
/* iOS and Windows reserve x18 for the system.  Disable Go closures until
   a new static chain is chosen.  */
#define FFI_GO_CLOSURES 1
#endif

#ifndef _M_ARM64
/* No complex type on Windows */
#define AA_FI_TARGET_HAS_COMPLEX_TYPE
#endif

#endif // TE_LIBFFI_TARGET_H
