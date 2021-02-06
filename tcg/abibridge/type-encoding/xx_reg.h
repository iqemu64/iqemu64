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

//===- x86_64 register --------------------------------------------*- C -*-===//

#ifndef TE_X86_64_REG_H
#define TE_X86_64_REG_H

enum x86_64_reg {
  xNoRegister = 0,
  TE_RAX, TE_EAX, TE_AX, TE_AL, // 4
  TE_RDI, TE_EDI, // 6
  TE_RSI, TE_ESI, // 8
  TE_RDX, TE_EDX, TE_DL, // 11
  TE_RCX, TE_ECX, // 13
  TE_R8, TE_R8D, // 15
  TE_R9, TE_R9D, // 17
  TE_XMM0, TE_XMM1, TE_XMM2, TE_XMM3, TE_XMM4, TE_XMM5, TE_XMM6, TE_XMM7, // 25
  TE_ST0, TE_ST1, TE_ST2, TE_ST3, TE_ST4, TE_ST5, TE_ST6, TE_ST7, // 33
  TE_FP0, TE_FP1, // 35
};

#endif // TE_X86_64_REG_H
