/* -----------------------------------------------------------------------
   prep_cif.c - Copyright (c) 2011, 2012  Anthony Green
                Copyright (c) 1996, 1998, 2007  Red Hat, Inc.
                Copyright (c) 2020 上海芯竹科技有限公司

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

#include "aa_internal.h"
#include "aa_reg.h"
#include "te_common.h"
#include "te_ffi.h"
#include <assert.h>
#include <stdlib.h>

// no need for sign/zero extends
static unsigned fakeLocInfo = TE_NO_LOC_INFO;

// save keystrokes
#define FNR_INFO(reg, fnr)                                                     \
  case AARCH64_RET_##reg##4:                                                   \
    te_set_reg(&fnr##info, 3, TE_##reg##3, fakeLocInfo);                       \
  case AARCH64_RET_##reg##3:                                                   \
    te_set_reg(&fnr##info, 2, TE_##reg##2, fakeLocInfo);                       \
  case AARCH64_RET_##reg##2:                                                   \
    te_set_reg(&fnr##info, 1, TE_##reg##1, fakeLocInfo);                       \
  case AARCH64_RET_##reg##1:                                                   \
    te_set_reg(&fnr##info, 0, TE_##reg##0, fakeLocInfo);                       \
    break

/* Force FFI_TYPE_LONGDOUBLE to be different than FFI_TYPE_DOUBLE;
   all further uses in this file will refer to the 128-bit type.  */
#if FFI_TYPE_DOUBLE != FFI_TYPE_LONGDOUBLE
#if FFI_TYPE_LONGDOUBLE != 4
#error FFI_TYPE_LONGDOUBLE out of date
#endif
#else
#undef FFI_TYPE_LONGDOUBLE
#define FFI_TYPE_LONGDOUBLE 4
#endif

/* Round up to AA_FI_SIZEOF_ARG. */

#define STACK_ARG_SIZE(x) FFI_ALIGN(x, FFI_SIZEOF_ARG)

/* Perform machine independent initialization of aggregate type
   specifications. */

static ffi_status initialize_aggregate(ffi_type *arg, size_t *offsets) {
  ffi_type **ptr;

  if (UNLIKELY(arg == NULL || arg->elements == NULL))
    return FFI_BAD_TYPEDEF;

  arg->size = 0;
  arg->alignment = 0;

  ptr = &(arg->elements[0]);

  if (UNLIKELY(ptr == 0))
    return FFI_BAD_TYPEDEF;

  while ((*ptr) != NULL) {
    if (UNLIKELY(((*ptr)->size == 0) &&
                 (initialize_aggregate((*ptr), NULL) != FFI_OK)))
      return FFI_BAD_TYPEDEF;

    arg->size = FFI_ALIGN(arg->size, (*ptr)->alignment);
    if (offsets)
      *offsets++ = arg->size;
    arg->size += (*ptr)->size;

    arg->alignment = (arg->alignment > (*ptr)->alignment) ? arg->alignment
                                                          : (*ptr)->alignment;

    ptr++;
  }

  /* Structure size includes tail padding.  This is important for
     structures that fit in one register on ABIs like the PowerPC64
     Linux ABI that right justify small structs in a register.
     It's also needed for nested structure layout, for example
     struct A { long a; char b; }; struct B { struct A x; char y; };
     should find y at an offset of 2*sizeof(long) and result in a
     total size of 3*sizeof(long).  */
  arg->size = FFI_ALIGN(arg->size, arg->alignment);

  /* On some targets, the ABI defines that structures have an additional
     alignment beyond the "natural" one based on their elements.  */
#ifdef AA_FI_AGGREGATE_ALIGNMENT
  if (AA_FI_AGGREGATE_ALIGNMENT > arg->alignment)
    arg->alignment = AA_FI_AGGREGATE_ALIGNMENT;
#endif

  if (arg->size == 0)
    return FFI_BAD_TYPEDEF;
  else
    return FFI_OK;
}

/* A subroutine of is_vfp_type.  Given a structure type, return the type code
   of the first non-structure element.  Recurse for structure elements.
   Return -1 if the structure is in fact empty, i.e. no nested elements.  */
static int is_hfa0(const ffi_type *ty) {
  ffi_type **elements = ty->elements;
  int i, ret = -1;

  if (elements != NULL)
    for (i = 0; elements[i]; ++i) {
      ret = elements[i]->type;
      if (ret == FFI_TYPE_STRUCT || ret == FFI_TYPE_COMPLEX) {
        ret = is_hfa0(elements[i]);
        if (ret < 0)
          continue;
      }
      break;
    }

  return ret;
}

/* A subroutine of is_vfp_type.  Given a structure type, return true if all
   of the non-structure elements are the same as CANDIDATE.  */
static int is_hfa1(const ffi_type *ty, int candidate) {
  ffi_type **elements = ty->elements;
  int i;

  if (elements != NULL)
    for (i = 0; elements[i]; ++i) {
      int t = elements[i]->type;
      if (t == FFI_TYPE_STRUCT || t == FFI_TYPE_COMPLEX) {
        if (!is_hfa1(elements[i], candidate))
          return 0;
      } else if (t != candidate)
        return 0;
    }

  return 1;
}

/* Determine if TY may be allocated to the FP registers.  This is both an
   fp scalar type as well as an homogenous floating point aggregate (HFA).
   That is, a structure consisting of 1 to 4 members of all the same type,
   where that type is an fp scalar.

   Returns non-zero iff TY is an HFA.  The result is the AARCH64_RET_*
   constant for the type.  */
static int is_vfp_type(const ffi_type *ty) {
  ffi_type **elements;
  int candidate, i;
  size_t size, ele_count;

  /* Quickest tests first.  */
  candidate = ty->type;
  switch (candidate) {
  default:
    return 0;
  case FFI_TYPE_FLOAT:
  case FFI_TYPE_DOUBLE:
  case FFI_TYPE_LONGDOUBLE:
    ele_count = 1;
    goto done;
  case FFI_TYPE_COMPLEX:
    candidate = ty->elements[0]->type;
    switch (candidate) {
    case FFI_TYPE_FLOAT:
    case FFI_TYPE_DOUBLE:
    case FFI_TYPE_LONGDOUBLE:
      ele_count = 2;
      goto done;
    }
    return 0;
  case FFI_TYPE_STRUCT:
    break;
  }

  /* No HFA types are smaller than 4 bytes, or larger than 64 bytes.  */
  size = ty->size;
  if (size < 4 || size > 64)
    return 0;

  /* Find the type of the first non-structure member.  */
  elements = ty->elements;
  candidate = elements[0]->type;
  if (candidate == FFI_TYPE_STRUCT || candidate == FFI_TYPE_COMPLEX) {
    for (i = 0;; ++i) {
      candidate = is_hfa0(elements[i]);
      if (candidate >= 0)
        break;
    }
  }

  /* If the first member is not a floating point type, it's not an HFA.
     Also quickly re-check the size of the structure.  */
  switch (candidate) {
  case FFI_TYPE_FLOAT:
    ele_count = size / sizeof(float);
    if (size != ele_count * sizeof(float))
      return 0;
    break;
  case FFI_TYPE_DOUBLE:
    ele_count = size / sizeof(double);
    if (size != ele_count * sizeof(double))
      return 0;
    break;
  case FFI_TYPE_LONGDOUBLE:
    ele_count = size / sizeof(long double);
    if (size != ele_count * sizeof(long double))
      return 0;
    break;
  default:
    return 0;
  }
  if (ele_count > 4)
    return 0;

  /* Finally, make sure that all scalar elements are the same type.  */
  for (i = 0; elements[i]; ++i) {
    int t = elements[i]->type;
    if (t == FFI_TYPE_STRUCT || t == FFI_TYPE_COMPLEX) {
      if (!is_hfa1(elements[i], candidate))
        return 0;
    } else if (t != candidate)
      return 0;
  }

  /* All tests succeeded.  Encode the result.  */
done:
  return candidate * 4 + (4 - (int)ele_count);
}

static void pass_in_memory(size_t *stack_offset_ptr, unsigned short alignment,
                           size_t ty_size, ABIFnInfo *fnInfo, int argIdx
#if defined(__APPLE__)
                           ,
                           unsigned allocating_variadic
#endif
) {
#if defined(__APPLE__)
  if (allocating_variadic && alignment < 8)
    alignment = 8;
#else
  if (alignment < 8)
    alignment = 8;
#endif
  size_t stack_offset = *stack_offset_ptr;
  stack_offset = FFI_ALIGN(stack_offset, alignment);
  if (fnInfo) {
    fnInfo->ainfo[argIdx].mask |= TE_MASK_ISMEM;
    te_set_mem(&fnInfo->ainfo[argIdx], stack_offset, fakeLocInfo);
  }
  stack_offset += ty_size;
  *stack_offset_ptr = stack_offset;
}

static ffi_status my_aa_fi_prep_cif_machdep(aa_fi_cif *cif, ABIFnInfo *fnInfo) {
  ffi_type *rtype = cif->rtype;
  size_t bytes = cif->bytes;
  int flags;

  switch (rtype->type) {
  case FFI_TYPE_VOID:
    flags = AARCH64_RET_VOID;
    break;
  case FFI_TYPE_UINT8:
    flags = AARCH64_RET_UINT8;
    break;
  case FFI_TYPE_UINT16:
    flags = AARCH64_RET_UINT16;
    break;
  case FFI_TYPE_UINT32:
    flags = AARCH64_RET_UINT32;
    break;
  case FFI_TYPE_SINT8:
    flags = AARCH64_RET_SINT8;
    break;
  case FFI_TYPE_SINT16:
    flags = AARCH64_RET_SINT16;
    break;
  case FFI_TYPE_INT:
  case FFI_TYPE_SINT32:
    flags = AARCH64_RET_SINT32;
    break;
  case FFI_TYPE_SINT64:
  case FFI_TYPE_UINT64:
    flags = AARCH64_RET_INT64;
    break;
  case FFI_TYPE_POINTER:
    flags = (sizeof(void *) == 4 ? AARCH64_RET_UINT32 : AARCH64_RET_INT64);
    break;

  case FFI_TYPE_FLOAT:
  case FFI_TYPE_DOUBLE:
  case FFI_TYPE_LONGDOUBLE:
  case FFI_TYPE_STRUCT:
  case FFI_TYPE_COMPLEX:
    flags = is_vfp_type(rtype);
    if (flags == 0) {
      size_t s = rtype->size;
      if (s > 16) {
        flags = AARCH64_RET_VOID | AARCH64_RET_IN_MEM;
        bytes += 8;
      } else if (s == 16)
        flags = AARCH64_RET_INT128;
      else if (s == 8)
        flags = AARCH64_RET_INT64;
      else
        flags = AARCH64_RET_INT128; //| AARCH64_RET_NEED_COPY;
    }
    break;

  default:
    assert(0 && "unreachable");
    abort();
  }
  // store return value info
  // not consider sign/zero extend
  if (fnInfo) {
    switch (flags) {
    case AARCH64_RET_VOID:
      te_set_reg(&fnInfo->rinfo, 0, NoRegister, TE_NO_LOC_INFO);
      break;

    case AARCH64_RET_INT64:
      te_set_reg(&fnInfo->rinfo, 0, TE_X0, fakeLocInfo);
      break;

    case AARCH64_RET_INT128:
      te_set_reg(&fnInfo->rinfo, 0, TE_X0, fakeLocInfo);
      te_set_reg(&fnInfo->rinfo, 1, TE_X1, fakeLocInfo);
      break;

      // macros
      FNR_INFO(S, fnInfo->r);
      FNR_INFO(D, fnInfo->r);
      FNR_INFO(Q, fnInfo->r);
      // end macros

    case AARCH64_RET_UINT8:
    case AARCH64_RET_UINT16:
    case AARCH64_RET_UINT32:
    case AARCH64_RET_SINT8:
    case AARCH64_RET_SINT16:
    case AARCH64_RET_SINT32:
      te_set_reg(&fnInfo->rinfo, 0, TE_W0, fakeLocInfo);
      break;
    default:
      assert(flags == (AARCH64_RET_VOID | AARCH64_RET_IN_MEM));
      // If the return value is 'struct return', its pointer is located in X8
      fnInfo->rinfo.mask |= TE_MASK_INDIRECT;
      te_set_reg(&fnInfo->rinfo, 0, TE_X8, fakeLocInfo);
    }
  }

  unsigned gprcount = 0; // general-purpose register
  unsigned ssecount = 0; // vector register
  size_t stack_offset = 0;
#if defined(__APPLE__)
  unsigned allocating_variadic = 0;
#endif
  const uint16_t gprcnt2reg[] = {TE_X0, TE_X1, TE_X2, TE_X3,
                                 TE_X4, TE_X5, TE_X6, TE_X7};
  const uint16_t ssecnt2reg[] = {TE_Q0, TE_Q1, TE_Q2, TE_Q3,
                                 TE_Q4, TE_Q5, TE_Q6, TE_Q7};
  // go over all args
  for (int i = 0, n = cif->nargs; i < n; ++i) {
    ffi_type *ty = cif->arg_types[i];
    size_t s = ty->size;
    int t = ty->type;
    if (fnInfo) {
      te_set_arg_size(&fnInfo->ainfo[i], s);
    }
    switch (t) {
    case FFI_TYPE_VOID:
      assert(0 && "arg void");
      break;
    case FFI_TYPE_INT:
    case FFI_TYPE_UINT8:
    case FFI_TYPE_SINT8:
    case FFI_TYPE_UINT16:
    case FFI_TYPE_SINT16:
    case FFI_TYPE_UINT32:
    case FFI_TYPE_SINT32:
    case FFI_TYPE_UINT64:
    case FFI_TYPE_SINT64:
    case FFI_TYPE_POINTER: {
      if (gprcount < N_X_ARG_REG) {
        if (fnInfo) {
          te_set_reg(&fnInfo->ainfo[i], 0, gprcnt2reg[gprcount], fakeLocInfo);
        }
        ++gprcount;
      } else {
        pass_in_memory(&stack_offset, ty->alignment, s, fnInfo, i
#if defined(__APPLE__)
                       ,
                       allocating_variadic
#endif
        );
        gprcount = N_X_ARG_REG;
      }
    } break;
    case FFI_TYPE_FLOAT:
    case FFI_TYPE_DOUBLE:
    case FFI_TYPE_LONGDOUBLE:
    case FFI_TYPE_STRUCT:
    case FFI_TYPE_COMPLEX: {
      int h = is_vfp_type(ty);
      if (h) {
        int elems = 4 - (h & 3);
#ifdef _M_ARM64 /* for handling armasm calling convention */
        if (cif->is_variadic) {
          if (gprcount + elems <= N_X_ARG_REG) {
            if (fnInfo) {
              for (int idxElem = 0; idxElem < elems; ++idxElem) {
                te_set_reg(&fnInfo->ainfo[i], idxElem, gprcnt2reg[gprcount + idxElem]);
              }
            }
            gprcount += elems;
            break;
          }
          ssecount = N_X_ARG_REG;
          pass_in_memory(&stack_offset, ty->alignment, s, fnInfo, i
#if defined(__APPLE__)
                         ,
                         allocating_variadic
#endif
          );

        } else {
#endif /* for handling armasm calling convention */
          if (ssecount + elems <= N_V_ARG_REG) {
            if (fnInfo) {
              for (int idxElem = 0; idxElem < elems; ++idxElem) {
                te_set_reg(&fnInfo->ainfo[i], idxElem, ssecnt2reg[ssecount + idxElem], fakeLocInfo);
              }
            }
            ssecount += elems;
            break;
          }
          ssecount = N_V_ARG_REG;
          pass_in_memory(&stack_offset, ty->alignment, s, fnInfo, i
#if defined(__APPLE__)
                         ,
                         allocating_variadic
#endif
          );
#ifdef _M_ARM64 /* for handling armasm calling convention */
        }
#endif /* for handling armasm calling convention */
      } else if (s > 16) {
        /* If the argument is a composite type that is larger than 16
           bytes, then the argument has been copied to memory, and
           the argument is replaced by a pointer to the copy.  */
        s = sizeof(void *);
        if (gprcount < N_X_ARG_REG) {
          if (fnInfo) {
            te_set_reg(&fnInfo->ainfo[i], 0, gprcnt2reg[gprcount], fakeLocInfo);
          }
          ++gprcount;
        } else {
          pass_in_memory(&stack_offset, ty->alignment, s, fnInfo, i
#if defined(__APPLE__)
                         ,
                         allocating_variadic
#endif
          );
          gprcount = N_X_ARG_REG;
        }
        // note: now the argument is passed indirectly
        if (fnInfo)
          fnInfo->ainfo[i].mask |= TE_MASK_INDIRECT;
        break;
      } else {
        size_t n = (s + 7) / 8;
        if (gprcount + n <= N_X_ARG_REG) {
          /* If the argument is a composite type and the size in
             double-words is not more than the number of available
             X registers, then the argument is copied into
             consecutive X registers.  */
          if (fnInfo) {
            for (int idxElem = 0; idxElem < n; ++idxElem) {
              te_set_reg(&fnInfo->ainfo[i], idxElem, gprcnt2reg[gprcount + idxElem], fakeLocInfo);
            }
          }
          gprcount += (unsigned int)n;
        } else {
          /* Otherwise, there are no enough X registers. Further
             X register allocations are prevented, the stack_offset is
             adjusted and the argument is copied to memory at the
             adjusted stack_offset.  */
          gprcount = N_X_ARG_REG;
          pass_in_memory(&stack_offset, ty->alignment, s, fnInfo, i
#if defined(__APPLE__)
                         ,
                         allocating_variadic
#endif
          );
        }
      }
    } break;
    default:
      assert(0 && "unreachable");
      abort();
    }

#if defined(__APPLE__)
    if (i + 1 == cif->aarch64_nfixedargs) {
      gprcount = N_X_ARG_REG;
      ssecount = N_V_ARG_REG;
      allocating_variadic = 1;
    }
#endif
  }

  /* Round the stack up to a multiple of the stack alignment requirement. */
  cif->bytes = (unsigned)FFI_ALIGN(bytes, 16);
  if (fnInfo)
    fnInfo->bytes = (unsigned)stack_offset;
  cif->flags = flags;
#if defined(__APPLE__)
  cif->aarch64_nfixedargs = 0;
#endif

  return FFI_OK;
}

#if defined(__APPLE__)
/* Perform Apple-specific cif processing for variadic calls */
static ffi_status my_aa_fi_prep_cif_machdep_var(aa_fi_cif *cif,
                                                unsigned int nfixedargs,
                                                unsigned int ntotalargs,
                                                ABIFnInfo *fnInfo) {
  cif->aarch64_nfixedargs = nfixedargs;
  ffi_status status = my_aa_fi_prep_cif_machdep(cif, fnInfo);
  return status;
}
#endif /* __APPLE__ */

#ifndef __CRIS__
/* The CRIS ABI specifies structure elements to have byte
   alignment only, so it completely overrides this functions,
   which assumes "natural" alignment and padding.  */

/* Perform machine independent aa_fi_cif preparation, then call
   machine dependent routine. */

/* For non variadic functions isvariadic should be 0 and
   nfixedargs==ntotalargs.

   For variadic calls, isvariadic should be 1 and nfixedargs
   and ntotalargs set as appropriate. nfixedargs must always be >=1 */
static ffi_status AA_FI_HIDDEN
my_aa_fi_prep_cif_core(aa_fi_cif *cif, aa_fi_abi abi, unsigned int isvariadic,
                       unsigned int nfixedargs, unsigned int ntotalargs,
                       ffi_type *rtype, ffi_type **atypes, ABIFnInfo *fnInfo) {
  unsigned bytes = 0;
  unsigned int i;
  ffi_type **ptr;

  assert(cif != NULL);
  assert((!isvariadic) || (nfixedargs >= 1));
  assert(nfixedargs <= ntotalargs);

  if (!(abi > AA_FI_FIRST_ABI && abi < AA_FI_LAST_ABI))
    return FFI_BAD_ABI;

  cif->abi = abi;
  cif->arg_types = atypes;
  cif->nargs = ntotalargs;
  cif->rtype = rtype;

  cif->flags = 0;
#ifdef _M_ARM64
  cif->is_variadic = isvariadic;
#endif
#if HAVE_LONG_DOUBLE_VARIANT
  aa_fi_prep_types(abi);
#endif

  /* Initialize the return type if necessary */
  if ((cif->rtype->size == 0) &&
      (initialize_aggregate(cif->rtype, NULL) != FFI_OK))
    return FFI_BAD_TYPEDEF;

#ifndef AA_FI_TARGET_HAS_COMPLEX_TYPE
  if (rtype->type == FFI_TYPE_COMPLEX)
    abort();
#endif

    /* x86, x86-64 and s390 stack space allocation is handled in prep_machdep.
     */
#if !defined AA_FI_TARGET_SPECIFIC_STACK_SPACE_ALLOCATION
  /* Make space for the return structure pointer */
  if (cif->rtype->type == FFI_TYPE_STRUCT
#ifdef TILE
      && (cif->rtype->size > 10 * AA_FI_SIZEOF_ARG)
#endif
#ifdef XTENSA
      && (cif->rtype->size > 16)
#endif
#ifdef NIOS2
      && (cif->rtype->size > 8)
#endif
  )
    bytes = STACK_ARG_SIZE(sizeof(void *));
#endif

  for (ptr = cif->arg_types, i = cif->nargs; i > 0; i--, ptr++) {

    /* Initialize any uninitialized aggregate type definitions */
    if (((*ptr)->size == 0) && (initialize_aggregate((*ptr), NULL) != FFI_OK))
      return FFI_BAD_TYPEDEF;

#ifndef AA_FI_TARGET_HAS_COMPLEX_TYPE
    if ((*ptr)->type == FFI_TYPE_COMPLEX)
      abort();
#endif

#if !defined AA_FI_TARGET_SPECIFIC_STACK_SPACE_ALLOCATION
    {
      /* Add any padding if necessary */
      if (((*ptr)->alignment - 1) & bytes)
        bytes = (unsigned)FFI_ALIGN(bytes, (*ptr)->alignment);

#ifdef TILE
      if (bytes < 10 * AA_FI_SIZEOF_ARG &&
          bytes + STACK_ARG_SIZE((*ptr)->size) > 10 * AA_FI_SIZEOF_ARG) {
        /* An argument is never split between the 10 parameter
           registers and the stack.  */
        bytes = 10 * AA_FI_SIZEOF_ARG;
      }
#endif
#ifdef XTENSA
      if (bytes <= 6 * 4 && bytes + STACK_ARG_SIZE((*ptr)->size) > 6 * 4)
        bytes = 6 * 4;
#endif

      bytes += (unsigned int)STACK_ARG_SIZE((*ptr)->size);
    }
#endif
  }

  cif->bytes = bytes;

  /* Perform machine dependent cif processing */
#ifdef AA_FI_TARGET_SPECIFIC_VARIADIC
  if (isvariadic)
    return my_aa_fi_prep_cif_machdep_var(cif, nfixedargs, ntotalargs, fnInfo);
#endif

  cif->aarch64_nfixedargs = 0;
  return my_aa_fi_prep_cif_machdep(cif, fnInfo);
}
#endif /* not __CRIS__ */

ffi_status my_aa_fi_prep_cif(aa_fi_cif *cif, aa_fi_abi abi, unsigned int nargs,
                             ffi_type *rtype, ffi_type **atypes,
                             ABIFnInfo *fnInfo) {
  return my_aa_fi_prep_cif_core(cif, abi, 0, nargs, nargs, rtype, atypes,
                                fnInfo);
}

ffi_status my_aa_fi_prep_cif_var(aa_fi_cif *cif, aa_fi_abi abi, unsigned nfixed,
                                 unsigned ntotal, ffi_type *rtype,
                                 ffi_type **atypes, ABIFnInfo *fnInfo) {
  return my_aa_fi_prep_cif_core(cif, abi, 1, nfixed, ntotal, rtype, atypes,
                                fnInfo);
}

/*
aa_fi_status aa_fi_get_struct_offsets(aa_fi_abi abi, ffi_type *struct_type,
                                      size_t *offsets) {
  if (!(abi > AA_FI_FIRST_ABI && abi < AA_FI_LAST_ABI))
    return AA_FI_BAD_ABI;
  if (struct_type->type != FFI_TYPE_STRUCT)
    return AA_FI_BAD_TYPEDEF;

#if HAVE_LONG_DOUBLE_VARIANT
  aa_fi_prep_types(abi);
#endif

  return initialize_aggregate(struct_type, offsets);
}
*/
