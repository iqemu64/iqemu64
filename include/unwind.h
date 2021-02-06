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

#ifndef unwind_h
#define unwind_h

#include <setjmp.h>

typedef unsigned __int128 xmm_register_t;

struct _Register_Context
{
    register_t                          rax;
    register_t                          rbx;
    register_t                          rcx;
    register_t                          rdx;
    register_t                          rdi;
    register_t                          rsi;
    register_t                          rbp;
    register_t                          rsp;
    register_t                          r8;
    register_t                          r9;
    register_t                          r10;
    register_t                          r11;
    register_t                          r12;
    register_t                          r13;
    register_t                          r14;
    register_t                          r15;
    xmm_register_t                      xmm0;
    xmm_register_t                      xmm1;
    xmm_register_t                      xmm2;
    xmm_register_t                      xmm3;
    xmm_register_t                      xmm4;
    xmm_register_t                      xmm5;
    xmm_register_t                      xmm6;
    xmm_register_t                      xmm7;
};

struct _Unwind_XloopContext
{
    intptr_t                            sig;        // should be (-1)
    struct _Unwind_FunctionContext*     prev;
    
    struct _Register_Context*           context;
    jmp_buf                             jbuf;
    
};

extern void _Unwind_SjLj_RegisterXloop(struct _Unwind_XloopContext *xc);
extern void _Unwind_SjLj_UnregisterXloop(struct _Unwind_XloopContext *xc);

#endif /* unwind_h */
