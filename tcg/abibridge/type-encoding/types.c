/* -----------------------------------------------------------------------
   types.c - Copyright (c) 1996, 1998  Red Hat, Inc.
   
   Predefined ffi_types needed by libffi.

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   ``Software''), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   ----------------------------------------------------------------------- */

/* Hide the basic type definitions from the header file, so that we
   can redefine them here as "const".  */
#define LIBFFI_HIDE_BASIC_TYPES

#include "te_ffi.h"
#include "te_common.h"

/* Type definitions */

#define FFI_TYPEDEF(name, type, id, maybe_const)\
struct struct_align_##name {			\
  char c;					\
  type x;					\
};						\
FFI_EXTERN					\
maybe_const ffi_type ffi_type_##name = {	\
  sizeof(type),					\
  offsetof(struct struct_align_##name, x),	\
  id, NULL					\
}

/* Size and alignment are fake here. They must not be 0. */
FFI_EXTERN const ffi_type ffi_type_void = {
  1, 1, FFI_TYPE_VOID, NULL
};

FFI_TYPEDEF(uint8, UINT8, FFI_TYPE_UINT8, const);
FFI_TYPEDEF(sint8, SINT8, FFI_TYPE_SINT8, const);
FFI_TYPEDEF(uint16, UINT16, FFI_TYPE_UINT16, const);
FFI_TYPEDEF(sint16, SINT16, FFI_TYPE_SINT16, const);
FFI_TYPEDEF(uint32, UINT32, FFI_TYPE_UINT32, const);
FFI_TYPEDEF(sint32, SINT32, FFI_TYPE_SINT32, const);
FFI_TYPEDEF(uint64, UINT64, FFI_TYPE_UINT64, const);
FFI_TYPEDEF(sint64, SINT64, FFI_TYPE_SINT64, const);

FFI_TYPEDEF(pointer, void*, FFI_TYPE_POINTER, const);

FFI_TYPEDEF(float, float, FFI_TYPE_FLOAT, const);
FFI_TYPEDEF(double, double, FFI_TYPE_DOUBLE, const);
