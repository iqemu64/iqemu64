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

//===- AArch64 register -------------------------------------------*- C -*-===//

#ifndef TE_AARCH64_REG_H
#define TE_AARCH64_REG_H

enum aarch64_reg {
  NoRegister = 0,
  TE_S0, TE_S1, TE_S2, TE_S3, TE_S4, TE_S5, TE_S6, TE_S7, // 8
  TE_D0, TE_D1, TE_D2, TE_D3, TE_D4, TE_D5, TE_D6, TE_D7, // 16
  TE_Q0, TE_Q1, TE_Q2, TE_Q3, TE_Q4, TE_Q5, TE_Q6, TE_Q7, // 24
  TE_W0, TE_W1, TE_W2, TE_W3, TE_W4, TE_W5, TE_W6, TE_W7, // 32
  TE_X0, TE_X1, TE_X2, TE_X3, TE_X4, TE_X5, TE_X6, TE_X7, TE_X8, // 41
};

#endif // TE_AARCH64_REG_H
