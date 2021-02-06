/*
 * Copyright (c) 2020 上海芯竹科技有限公司
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

//===- ABIArgInfo -------------------------------------------------*- C -*-===//

#ifndef TE_ABI_ARG_INFO_H
#define TE_ABI_ARG_INFO_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

static const unsigned STFP_LIST_TERMINATOR = ~0U;
enum stfpKind {
  STFP_FUNPTR = 0,
  STFP_STRUCT,
};

/*
// low level reg info
typedef struct {
  uint8_t regloc;
  uint8_t locInfo;
} ABIRegInfo; // 2 bytes

typedef struct {
  uint32_t memloc;
  uint32_t locInfo;
} ABIMemInfo; // 8 bytes
*/

enum {
  TE_MAX_REGS_FOR_ONE_ARG = 4,
};

enum {
  TE_MASK_INDIRECT = (1 << 0),
  TE_MASK_ISMEM    = (1 << 1),
  TE_MASK_CALLBACK = (1 << 2),
  TE_MASK_BLOCK    = (1 << 3),
  TE_MASK_STFP     = (1 << 4),
};

enum {
  TE_NO_LOC_INFO = 0,
  TE_Full, TE_BCvt,
  TE_AExt,
  TE_SExt8_32, TE_SExt16_32, // TE_SExt32_64, ...
  TE_ZExt8_32, TE_ZExt16_32, // TE_ZExt32_64, ...
};

// Represent an individual argument or return value.
// One argument could take multiple registers. Assume the maximum value is 4.
// An argument could be passed Directly or by pointer.
typedef struct {
  // union val {
  //   ABIMemInfo mem;
  //   ABIRegInfo regs[4];
  // };
  uint64_t val;
  uint32_t size;
  uint32_t mask;
} ABIArgInfo; // 16 bytes

static inline bool te_is_mem(ABIArgInfo x) {
  return (x.mask & TE_MASK_ISMEM);
}

static inline bool te_is_indirect(ABIArgInfo x) {
  return (x.mask & TE_MASK_INDIRECT);
}

static inline bool te_is_block(ABIArgInfo x) {
  return (x.mask & TE_MASK_BLOCK);
}

static inline bool te_is_callback(ABIArgInfo x) {
  return (x.mask & TE_MASK_CALLBACK);
}

static inline bool te_is_stfp(ABIArgInfo x) {
  return (x.mask & TE_MASK_STFP);
}

static inline unsigned te_num_of_reg(ABIArgInfo x) {
  uint16_t *reg = (uint16_t *)&x.val;
  unsigned cnt = 0;
  while (*reg && cnt < TE_MAX_REGS_FOR_ONE_ARG) {
    ++reg;
    ++cnt;
  }
  return cnt;
}

static inline unsigned te_get_reg_locinfo(ABIArgInfo x, unsigned idx) {
  uint8_t *reg = (uint8_t *)&x.val;
  return *(reg + idx * 2 + 1);
}

static inline unsigned te_get_reg(ABIArgInfo x, unsigned idx) {
  assert(idx < TE_MAX_REGS_FOR_ONE_ARG);
  uint8_t *reg = (uint8_t *)&x.val;
  return *(reg + idx * 2);
}

static inline void te_set_reg(ABIArgInfo *x, unsigned idx, unsigned val,
                              unsigned info) {
  assert(idx < TE_MAX_REGS_FOR_ONE_ARG);
  uint8_t *reg = (uint8_t *)&x->val;
  *(reg + idx * 2) = val;
  *(reg + idx * 2 + 1) = info;
}

static inline unsigned te_get_mem_locinfo(ABIArgInfo x) {
  uint32_t *mem = (uint32_t *)&x.val;
  return *(mem + 1);
}

static inline unsigned te_get_mem(ABIArgInfo x) {
  uint32_t *mem = (uint32_t *)&x.val;
  return *mem;
}

static inline void te_set_mem(ABIArgInfo *x, size_t val, unsigned info) {
  uint32_t *mem = (uint32_t *)&x->val;
  *mem = (uint32_t)val;
  *(mem + 1) = info;
}

static inline unsigned te_get_arg_size(ABIArgInfo x) {
  return x.size;
}

static inline void te_set_arg_size(ABIArgInfo *x, size_t val) {
  x->size = (uint32_t)val;
}

typedef struct {
  // const char *fnName; // identifier. you can use something else
  ABIArgInfo *ainfo;  // arguments
  ABIArgInfo rinfo;   // return value

  // If the argument/return is a struct with function pointer, then it has a
  // STFP_LIST_TERMINATOR terminated offset list to record those "important"
  // fields.
  // Lower 16 bits hold the `offsetof(struct, field)`, and higher 16 bits
  // indicate the kind of the field.
  // For higher 16 bits, we have
  // 0 => function pointer
  // 1 => the struct itself (consider struct Block_byref, field forwarding)
  uint32_t **argStfpOffsetLists;
  uint32_t *retStfpOffsetList;

  unsigned nargs;
  int bytes; // how many bytes do the parameters on the stack take

  // For calls that may call functions that use varargs or stdargs
  // (prototype-less calls or calls to functions containing ellipsis (...) in
  // the declaration) %al is used as hidden argument to specify the number of
  // vector registers used. The contents of %al do not need to match exactly
  // the number of registers, but must be an upper bound on the number of vector
  // registers used and is in the range 0–8 inclusive.
  unsigned ssecount;

  // uint8_t thisPos; // the `this` pointer

  // rinfo.indirect indicates whether the return value is struct return.
  // If it is, then sretPos is 0 in the json file.

  uint8_t isCxxStructor; // CxxStructor may return `this`

  uint8_t variadic;
} ABIFnInfo;

#endif // TE_ABI_ARG_INFO_H
