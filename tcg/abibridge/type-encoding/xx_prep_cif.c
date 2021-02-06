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

#include "te_common.h"
#include "te_ffi.h"
#include "xx_internal.h"
#include "xx_reg.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_GPR_REGS 6
#define MAX_SSE_REGS 8
#define MAX_CLASSES 4

enum x86_64_reg_class {
  X86_64_NO_CLASS,
  X86_64_INTEGER_CLASS,
  X86_64_INTEGERSI_CLASS,
  X86_64_SSE_CLASS,
  X86_64_SSESF_CLASS,
  X86_64_SSEDF_CLASS,
  X86_64_SSEUP_CLASS,
  X86_64_X87_CLASS,
  X86_64_X87UP_CLASS,
  X86_64_COMPLEX_X87_CLASS,
  X86_64_MEMORY_CLASS
};

#define SSE_CLASS_P(X) ((X) >= X86_64_SSE_CLASS && X <= X86_64_SSEUP_CLASS)

static enum x86_64_reg_class merge_classes(enum x86_64_reg_class class1,
                                           enum x86_64_reg_class class2) {
  /* Rule #1: If both classes are equal, this is the resulting class.  */
  if (class1 == class2)
    return class1;

  /* Rule #2: If one of the classes is NO_CLASS, the resulting class is
     the other class.  */
  if (class1 == X86_64_NO_CLASS)
    return class2;
  if (class2 == X86_64_NO_CLASS)
    return class1;

  /* Rule #3: If one of the classes is MEMORY, the result is MEMORY.  */
  if (class1 == X86_64_MEMORY_CLASS || class2 == X86_64_MEMORY_CLASS)
    return X86_64_MEMORY_CLASS;

  /* Rule #4: If one of the classes is INTEGER, the result is INTEGER.  */
  if ((class1 == X86_64_INTEGERSI_CLASS && class2 == X86_64_SSESF_CLASS) ||
      (class2 == X86_64_INTEGERSI_CLASS && class1 == X86_64_SSESF_CLASS))
    return X86_64_INTEGERSI_CLASS;
  if (class1 == X86_64_INTEGER_CLASS || class1 == X86_64_INTEGERSI_CLASS ||
      class2 == X86_64_INTEGER_CLASS || class2 == X86_64_INTEGERSI_CLASS)
    return X86_64_INTEGER_CLASS;

  /* Rule #5: If one of the classes is X87, X87UP, or COMPLEX_X87 class,
     MEMORY is used.  */
  if (class1 == X86_64_X87_CLASS || class1 == X86_64_X87UP_CLASS ||
      class1 == X86_64_COMPLEX_X87_CLASS || class2 == X86_64_X87_CLASS ||
      class2 == X86_64_X87UP_CLASS || class2 == X86_64_COMPLEX_X87_CLASS)
    return X86_64_MEMORY_CLASS;

  /* Rule #6: Otherwise class SSE is used.  */
  return X86_64_SSE_CLASS;
}

static size_t classify_argument(ffi_type *type, enum x86_64_reg_class classes[],
                                size_t byte_offset) {
  switch (type->type) {
  case FFI_TYPE_UINT8:
  case FFI_TYPE_SINT8:
  case FFI_TYPE_UINT16:
  case FFI_TYPE_SINT16:
  case FFI_TYPE_UINT32:
  case FFI_TYPE_SINT32:
  case FFI_TYPE_UINT64:
  case FFI_TYPE_SINT64:
  case FFI_TYPE_POINTER:
  do_integer : {
    size_t size = byte_offset + type->size;

    if (size <= 4) {
      classes[0] = X86_64_INTEGERSI_CLASS;
      return 1;
    } else if (size <= 8) {
      classes[0] = X86_64_INTEGER_CLASS;
      return 1;
    } else if (size <= 12) {
      classes[0] = X86_64_INTEGER_CLASS;
      classes[1] = X86_64_INTEGERSI_CLASS;
      return 2;
    } else if (size <= 16) {
      classes[0] = classes[1] = X86_64_INTEGER_CLASS;
      return 2;
    } else
      assert(0);
  }
  case FFI_TYPE_FLOAT:
    if (!(byte_offset % 8))
      classes[0] = X86_64_SSESF_CLASS;
    else
      classes[0] = X86_64_SSE_CLASS;
    return 1;
  case FFI_TYPE_DOUBLE:
    classes[0] = X86_64_SSEDF_CLASS;
    return 1;
#if FFI_TYPE_LONGDOUBLE != FFI_TYPE_DOUBLE
  case FFI_TYPE_LONGDOUBLE:
    classes[0] = X86_64_X87_CLASS;
    classes[1] = X86_64_X87UP_CLASS;
    return 2;
#endif
  case FFI_TYPE_STRUCT: {
    const size_t UNITS_PER_WORD = 8;
    size_t words = (type->size + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
    ffi_type **ptr;
    unsigned int i;
    enum x86_64_reg_class subclasses[MAX_CLASSES];

    /* If the struct is larger than 32 bytes, pass it on the stack.  */
    if (type->size > 32)
      return 0;

    for (i = 0; i < words; i++)
      classes[i] = X86_64_NO_CLASS;

    /* Zero sized arrays or structures are NO_CLASS.  We return 0 to
       signalize memory class, so handle it as special case.  */
    if (!words) {
    case FFI_TYPE_VOID:
      classes[0] = X86_64_NO_CLASS;
      return 1;
    }

    /* Merge the fields of structure.  */
    for (ptr = type->elements; *ptr != NULL; ptr++) {
      size_t num;

      byte_offset = FFI_ALIGN(byte_offset, (*ptr)->alignment);

      num = classify_argument(*ptr, subclasses, byte_offset % 8);
      if (num == 0)
        return 0;
      for (i = 0; i < num; i++) {
        size_t pos = byte_offset / 8;
        classes[i + pos] = merge_classes(subclasses[i], classes[i + pos]);
      }

      byte_offset += (*ptr)->size;
    }

    if (words > 2) {
      /* When size > 16 bytes, if the first one isn't
         X86_64_SSE_CLASS or any other ones aren't
         X86_64_SSEUP_CLASS, everything should be passed in
         memory.  */
      if (classes[0] != X86_64_SSE_CLASS)
        return 0;

      for (i = 1; i < words; i++)
        if (classes[i] != X86_64_SSEUP_CLASS)
          return 0;
    }

    /* Final merger cleanup.  */
    for (i = 0; i < words; i++) {
      /* If one class is MEMORY, everything should be passed in
         memory.  */
      if (classes[i] == X86_64_MEMORY_CLASS)
        return 0;

      /* The X86_64_SSEUP_CLASS should be always preceded by
         X86_64_SSE_CLASS or X86_64_SSEUP_CLASS.  */
      if (i > 1 && classes[i] == X86_64_SSEUP_CLASS &&
          classes[i - 1] != X86_64_SSE_CLASS &&
          classes[i - 1] != X86_64_SSEUP_CLASS) {
        /* The first one should never be X86_64_SSEUP_CLASS.  */
        assert(i != 0);
        classes[i] = X86_64_SSE_CLASS;
      }

      /*  If X86_64_X87UP_CLASS isn't preceded by X86_64_X87_CLASS,
          everything should be passed in memory.  */
      if (i > 1 && classes[i] == X86_64_X87UP_CLASS &&
          (classes[i - 1] != X86_64_X87_CLASS)) {
        /* The first one should never be X86_64_X87UP_CLASS.  */
        assert(i != 0);
        return 0;
      }
    }
    return words;
  }
  case FFI_TYPE_COMPLEX: {
    ffi_type *inner = type->elements[0];
    switch (inner->type) {
    case FFI_TYPE_INT:
    case FFI_TYPE_UINT8:
    case FFI_TYPE_SINT8:
    case FFI_TYPE_UINT16:
    case FFI_TYPE_SINT16:
    case FFI_TYPE_UINT32:
    case FFI_TYPE_SINT32:
    case FFI_TYPE_UINT64:
    case FFI_TYPE_SINT64:
      goto do_integer;

    case FFI_TYPE_FLOAT:
      classes[0] = X86_64_SSE_CLASS;
      if (byte_offset % 8) {
        classes[1] = X86_64_SSESF_CLASS;
        return 2;
      }
      return 1;
    case FFI_TYPE_DOUBLE:
      classes[0] = classes[1] = X86_64_SSEDF_CLASS;
      return 2;
#if FFI_TYPE_LONGDOUBLE != FFI_TYPE_DOUBLE
    case FFI_TYPE_LONGDOUBLE:
      classes[0] = X86_64_COMPLEX_X87_CLASS;
      return 1;
#endif
    }
  }
  }
  abort();
}

/* Examine the argument and return set number of register required in each
   class.  Return zero iff parameter should be passed in memory, otherwise
   the number of registers.  */
static size_t examine_argument(ffi_type *type,
                               enum x86_64_reg_class classes[MAX_CLASSES],
                               _Bool in_return, int *pngpr, int *pnsse) {
  size_t n;
  unsigned int i;
  int ngpr, nsse;

  n = classify_argument(type, classes, 0);
  if (n == 0)
    return 0;

  ngpr = nsse = 0;
  for (i = 0; i < n; ++i)
    switch (classes[i]) {
    case X86_64_INTEGER_CLASS:
    case X86_64_INTEGERSI_CLASS:
      ngpr++;
      break;
    case X86_64_SSE_CLASS:
    case X86_64_SSESF_CLASS:
    case X86_64_SSEDF_CLASS:
      nsse++;
      break;
    case X86_64_NO_CLASS:
    case X86_64_SSEUP_CLASS:
      break;
    case X86_64_X87_CLASS:
    case X86_64_X87UP_CLASS:
    case X86_64_COMPLEX_X87_CLASS:
      return in_return != 0;
    default:
      abort();
    }

  *pngpr = ngpr;
  *pnsse = nsse;

  return n;
}

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
#ifdef XX_FI_AGGREGATE_ALIGNMENT
  if (XX_FI_AGGREGATE_ALIGNMENT > arg->alignment)
    arg->alignment = XX_FI_AGGREGATE_ALIGNMENT;
#endif

  if (arg->size == 0)
    return FFI_BAD_TYPEDEF;
  else
    return FFI_OK;
}

static ffi_status my_xx_fi_prep_cif_machdep(xx_fi_cif *cif, ABIFnInfo *fnInfo) {
  enum x86_64_reg_class classes[MAX_CLASSES];
  int gprcount, ssecount, i, avn, ngpr, nsse;
  unsigned flags;
  size_t bytes, n, rtype_size;
  ffi_type *rtype;

#ifndef __ILP32__
  if (cif->abi == XX_FI_EFI64 || cif->abi == XX_FI_GNUW64) {
    puts("bad abi");
    abort();
    // return xx_fi_prep_cif_machdep_efi64(cif);
  }
#endif
  if (cif->abi != XX_FI_UNIX64)
    return FFI_BAD_ABI;

  gprcount = ssecount = 0;

  rtype = cif->rtype;
  rtype_size = rtype->size;
  switch (rtype->type) {
  case FFI_TYPE_VOID:
    flags = UNIX64_RET_VOID;
    break;
  case FFI_TYPE_UINT8:
    flags = UNIX64_RET_UINT8;
    break;
  case FFI_TYPE_SINT8:
    flags = UNIX64_RET_SINT8;
    break;
  case FFI_TYPE_UINT16:
    flags = UNIX64_RET_UINT16;
    break;
  case FFI_TYPE_SINT16:
    flags = UNIX64_RET_SINT16;
    break;
  case FFI_TYPE_UINT32:
    flags = UNIX64_RET_UINT32;
    break;
  case FFI_TYPE_INT:
  case FFI_TYPE_SINT32:
    flags = UNIX64_RET_SINT32;
    break;
  case FFI_TYPE_UINT64:
  case FFI_TYPE_SINT64:
    flags = UNIX64_RET_INT64;
    break;
  case FFI_TYPE_POINTER:
    flags = (sizeof(void *) == 4 ? UNIX64_RET_UINT32 : UNIX64_RET_INT64);
    break;
  case FFI_TYPE_FLOAT:
    flags = UNIX64_RET_XMM32;
    break;
  case FFI_TYPE_DOUBLE:
    flags = UNIX64_RET_XMM64;
    break;
#if FFI_TYPE_LONGDOUBLE != FFI_TYPE_DOUBLE
  case FFI_TYPE_LONGDOUBLE:
    flags = UNIX64_RET_X87;
    break;
#endif
  case FFI_TYPE_STRUCT:
    n = examine_argument(cif->rtype, classes, 1, &ngpr, &nsse);
    if (n == 0) {
      /* The return value is passed in memory.  A pointer to that
         memory is the first argument.  Allocate a register for it.  */
      gprcount++;
      /* We don't have to do anything in asm for the return.  */
      flags = UNIX64_RET_VOID | UNIX64_FLAG_RET_IN_MEM;
    } else {
      _Bool sse0 = SSE_CLASS_P(classes[0]);

      if (rtype_size == 4 && sse0)
        flags = UNIX64_RET_XMM32;
      else if (rtype_size == 8)
        flags = sse0 ? UNIX64_RET_XMM64 : UNIX64_RET_INT64;
      else {
        _Bool sse1 = (n == 2) && SSE_CLASS_P(classes[1]);
        if (sse0 && sse1)
          flags = UNIX64_RET_ST_XMM0_XMM1;
        else if (sse0)
          flags = UNIX64_RET_ST_XMM0_RAX;
        else if (sse1)
          flags = UNIX64_RET_ST_RAX_XMM0;
        else
          flags = UNIX64_RET_ST_RAX_RDX;
        // flags |= rtype_size << UNIX64_SIZE_SHIFT;
      }
    }
    break;
  case FFI_TYPE_COMPLEX:
    switch (rtype->elements[0]->type) {
    case FFI_TYPE_UINT8:
    case FFI_TYPE_SINT8:
    case FFI_TYPE_UINT16:
    case FFI_TYPE_SINT16:
    case FFI_TYPE_INT:
    case FFI_TYPE_UINT32:
    case FFI_TYPE_SINT32:
    case FFI_TYPE_UINT64:
    case FFI_TYPE_SINT64:
      flags = UNIX64_RET_ST_RAX_RDX; // | ((unsigned)rtype_size <<
                                     // UNIX64_SIZE_SHIFT);
      break;
    case FFI_TYPE_FLOAT:
      flags = UNIX64_RET_XMM64;
      break;
    case FFI_TYPE_DOUBLE:
      flags = UNIX64_RET_ST_XMM0_XMM1; //| (16 << UNIX64_SIZE_SHIFT);
      break;
#if FFI_TYPE_LONGDOUBLE != FFI_TYPE_DOUBLE
    case FFI_TYPE_LONGDOUBLE:
      flags = UNIX64_RET_X87_2;
      break;
#endif
    default:
      return FFI_BAD_TYPEDEF;
    }
    break;
  default:
    return FFI_BAD_TYPEDEF;
  }
  // store return value info
  if (fnInfo) {
    if (flags == UNIX64_RET_VOID) {
      te_set_reg(&fnInfo->rinfo, 0, xNoRegister, TE_NO_LOC_INFO);
    } else if (flags == UNIX64_RET_UINT8) {
      te_set_reg(&fnInfo->rinfo, 0, TE_AL, TE_ZExt8_32);
    } else if (flags == UNIX64_RET_SINT8) {
      te_set_reg(&fnInfo->rinfo, 0, TE_AL, TE_SExt8_32);
    } else if (flags == UNIX64_RET_UINT16) {
      te_set_reg(&fnInfo->rinfo, 0, TE_AX, TE_ZExt16_32);
    } else if (flags == UNIX64_RET_SINT16) {
      te_set_reg(&fnInfo->rinfo, 0, TE_AX, TE_SExt16_32);
    } else if (flags == UNIX64_RET_UINT32 || flags == UNIX64_RET_SINT32) {
      te_set_reg(&fnInfo->rinfo, 0, TE_EAX, TE_Full);
    } else if (flags == UNIX64_RET_INT64) {
      te_set_reg(&fnInfo->rinfo, 0, TE_RAX, TE_Full);
    } else if (flags == UNIX64_RET_XMM32 || flags == UNIX64_RET_XMM64) {
      te_set_reg(&fnInfo->rinfo, 0, TE_XMM0, TE_Full);
    } else if (flags == UNIX64_RET_X87) {
      te_set_reg(&fnInfo->rinfo, 0, TE_ST0, TE_Full);
    } else if (flags == UNIX64_RET_X87_2) {
      te_set_reg(&fnInfo->rinfo, 0, TE_ST0, TE_Full);
      te_set_reg(&fnInfo->rinfo, 1, TE_ST1, TE_Full);
    } else if (flags == UNIX64_RET_ST_XMM0_RAX) {
      te_set_reg(&fnInfo->rinfo, 0, TE_XMM0, TE_Full);
      te_set_reg(&fnInfo->rinfo, 1, TE_RAX, TE_Full);
    } else if (flags == UNIX64_RET_ST_RAX_XMM0) {
      te_set_reg(&fnInfo->rinfo, 0, TE_RAX, TE_Full);
      te_set_reg(&fnInfo->rinfo, 1, TE_XMM0, TE_Full);
    } else if (flags == UNIX64_RET_ST_XMM0_XMM1) {
      te_set_reg(&fnInfo->rinfo, 0, TE_XMM0, TE_Full);
      te_set_reg(&fnInfo->rinfo, 1, TE_XMM1, TE_Full);
    } else if (flags == UNIX64_RET_ST_RAX_RDX) {
      te_set_reg(&fnInfo->rinfo, 0, TE_RAX, TE_Full);
      te_set_reg(&fnInfo->rinfo, 1, TE_RDX, TE_Full);
    } else {
      assert(flags == (UNIX64_RET_VOID | UNIX64_FLAG_RET_IN_MEM));
      // If the return value is 'struct return', its pointer is passed as the
      // first integer argument.
      fnInfo->rinfo.mask |= TE_MASK_INDIRECT;
      te_set_reg(&fnInfo->rinfo, 0, TE_RDI, TE_Full);
    }
  }

  uint16_t gprcnt2reg[] = {TE_RDI, TE_RSI, TE_RDX, TE_RCX, TE_R8, TE_R9};
  uint16_t ssecnt2reg[] = {TE_XMM0, TE_XMM1, TE_XMM2, TE_XMM3,
                           TE_XMM4, TE_XMM5, TE_XMM6, TE_XMM7};
  /* Go over all arguments and determine the way they should be passed.
     If it's in a register and there is space for it, let that be so. If
     not, add it's size to the stack byte count.  */
  for (bytes = 0, i = 0, avn = cif->nargs; i < avn; i++) {
    size_t size = cif->arg_types[i]->size;
    if (fnInfo) {
      te_set_arg_size(&fnInfo->ainfo[i], size);
    }
    size_t n = examine_argument(cif->arg_types[i], classes, 0, &ngpr, &nsse);
    if (n == 0 || gprcount + ngpr > MAX_GPR_REGS ||
        ssecount + nsse > MAX_SSE_REGS) {
      long align = cif->arg_types[i]->alignment;

      if (align < 8)
        align = 8;

      bytes = FFI_ALIGN(bytes, align);
      if (fnInfo) {
        fnInfo->ainfo[i].mask |= TE_MASK_ISMEM;
        unsigned locInfo = TE_Full;
        ffi_type *type = cif->arg_types[i];
        switch (type->type) {
        case FFI_TYPE_SINT8:
          locInfo = TE_SExt8_32;
          break;

        case FFI_TYPE_SINT16:
          locInfo = TE_SExt16_32;
          break;

        case FFI_TYPE_UINT8:
          locInfo = TE_ZExt8_32;
          break;

        case FFI_TYPE_UINT16:
          locInfo = TE_ZExt16_32;
          break;

        default:
          break;
        }
        te_set_mem(&fnInfo->ainfo[i], bytes, locInfo);
      }
      bytes += size;
    } else {
      /* The argument is passed entirely in registers.  */
      for (unsigned j = 0, idxReg = 0; j < n; ++j, size -= 8) {
        switch (classes[j]) {
        case X86_64_NO_CLASS:
        case X86_64_SSEUP_CLASS:
          break;
        case X86_64_INTEGER_CLASS:
        case X86_64_INTEGERSI_CLASS:
          // maybe distinguish various INT size
          switch (cif->arg_types[i]->type) {
          case FFI_TYPE_SINT8:
            if (fnInfo) {
              assert(idxReg < 4 && "out of range");
              te_set_reg(&fnInfo->ainfo[i], idxReg, gprcnt2reg[gprcount], TE_SExt8_32);
              ++idxReg;
            }
            break;

          case FFI_TYPE_SINT16:
            if (fnInfo) {
              assert(idxReg < 4 && "out of range");
              te_set_reg(&fnInfo->ainfo[i], idxReg, gprcnt2reg[gprcount], TE_SExt16_32);
              ++idxReg;
            }
            break;

          case FFI_TYPE_UINT8:
            if (fnInfo) {
              assert(idxReg < 4 && "out of range");
              te_set_reg(&fnInfo->ainfo[i], idxReg, gprcnt2reg[gprcount], TE_ZExt8_32);
              ++idxReg;
            }
            break;

          case FFI_TYPE_UINT16:
            if (fnInfo) {
              assert(idxReg < 4 && "out of range");
              te_set_reg(&fnInfo->ainfo[i], idxReg, gprcnt2reg[gprcount], TE_ZExt16_32);
              ++idxReg;
            }
            break;

          default:
            if (fnInfo) {
              assert(idxReg < 4 && "out of range");
              te_set_reg(&fnInfo->ainfo[i], idxReg,gprcnt2reg[gprcount], TE_Full);
              ++idxReg;
            }
          }
          ++gprcount;
          break;
        case X86_64_SSE_CLASS:
        case X86_64_SSEDF_CLASS:
        case X86_64_SSESF_CLASS:
          if (fnInfo) {
            assert(idxReg < 4 && "out of range");
            te_set_reg(&fnInfo->ainfo[i], idxReg, ssecnt2reg[ssecount], TE_Full);
            ++idxReg;
          }
          ++ssecount;
          break;
        default:
          puts("unreachable");
          abort();
        }
      }
    }
  }
  if (ssecount)
    flags |= UNIX64_FLAG_XMM_ARGS;

  cif->flags = flags;
  cif->bytes = (unsigned)FFI_ALIGN(bytes, 8);
  if (fnInfo) {
    fnInfo->bytes = cif->bytes;
    fnInfo->ssecount = ssecount;
  }

  return FFI_OK;
}

static ffi_status
my_xx_fi_prep_cif_core(xx_fi_cif *cif, xx_fi_abi abi, unsigned isvariadic,
                       unsigned int nfixedargs, unsigned int ntotalargs,
                       ffi_type *rtype, ffi_type **atypes, ABIFnInfo *fnInfo) {
  // remove inactive macros
  assert(cif != NULL);
  assert((!isvariadic) || (nfixedargs >= 1));
  assert(nfixedargs <= ntotalargs);

  if (!(abi > XX_FI_FIRST_ABI && abi < XX_FI_LAST_ABI))
    return FFI_BAD_ABI;

  cif->abi = abi;
  cif->arg_types = atypes;
  cif->nargs = ntotalargs;
  cif->rtype = rtype;
  cif->bytes = 0;
  cif->flags = 0;
  /* Initialize the return type if necessary */
  if ((cif->rtype->size == 0) &&
      (initialize_aggregate(cif->rtype, NULL) != FFI_OK))
    return FFI_BAD_TYPEDEF;

  unsigned i;
  ffi_type **ptr;
  for (ptr = cif->arg_types, i = cif->nargs; i > 0; i--, ptr++) {
    /* Initialize any uninitialized aggregate type definitions */
    if (((*ptr)->size == 0) && (initialize_aggregate((*ptr), NULL) != FFI_OK))
      return FFI_BAD_TYPEDEF;
  }
  return my_xx_fi_prep_cif_machdep(cif, fnInfo);
}

ffi_status my_xx_fi_prep_cif(xx_fi_cif *cif, xx_fi_abi abi, unsigned nargs,
                             ffi_type *rtype, ffi_type **atypes,
                             ABIFnInfo *fnInfo) {
  return my_xx_fi_prep_cif_core(cif, abi, 0, nargs, nargs, rtype, atypes,
                                fnInfo);
}
