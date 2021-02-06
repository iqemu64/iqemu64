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

.macro ENTRY
    .text
    .globl    $0
    .align    2, 0x90
$0:
.endmacro

    .data
    .align 8
    .private_extern _getLazyBindingInfo
_getLazyBindingInfo:        .quad 0

    ENTRY   ___MygetLazyBindingInfo
    .private_extern ___MygetLazyBindingInfo

    subq    $0x8, %rsp       // alignment
    pushq   %rdi             // save parameters for next call
    pushq   %rsi
    pushq   %rdx
    pushq   %rcx
    pushq   %r8
    pushq   %r9
    pushq   0x48(%rsp)
    pushq   0x48(%rsp)

    call    L__stub_for_orig

    movq    %rax, 0x40(%rsp)
    movq    0x38(%rsp), %rdi
    movq    0x30(%rsp), %rsi
    movq    0x28(%rsp), %rdx
    movq    0x20(%rsp), %rcx
    movq    0x18(%rsp), %r8
    movq    0x10(%rsp), %r9

    call    _MygetLazyBindingInfo
    movq    0x40(%rsp), %rax
    addq    $0x48, %rsp
    ret

L__stub_for_orig:

    pushq   %rbp        // instructions that've been overwritten
    movq    %rsp, %rbp
    pushq   %r15
    pushq   %r14
    pushq   %r13
    pushq   %r12

    movq    _getLazyBindingInfo(%rip), %r15
    addq    $0xC, %r15
    jmp     *%r15
