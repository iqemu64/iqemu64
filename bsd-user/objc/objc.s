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

#include <Availability.h>

.macro ENTRY
    .text
    .globl    $0
    .align    2, 0x90
$0:
.endmacro

    .data
    .align 8
    .private_extern _objc_getMethodNoSuper_nolock
_objc_getMethodNoSuper_nolock:    .quad 0

    ENTRY   ___MygetMethodNoSuper_nolock
    .private_extern ___MygetMethodNoSuper_nolock

    subq    $0x8, %rsp       // alignment
    pushq   %rdi             // save parameters for next call
    pushq   %rsi

    call    L__stub_for_orig

    movq    %rax, %rdx      // third parameter, the result of the last call
    movq    0x8(%rsp), %rdi
    movq    (%rsp), %rsi

    call    _MygetMethodNoSuper_nolock

    addq    $0x18, %rsp
    ret

L__stub_for_orig:

    pushq   %rbp        // instructions that've been overwritten
    pushq   %r15
    pushq   %r14
    pushq   %rbx
    movq    $0x7FFFFFFFFFF8, %rax
    movq    _objc_getMethodNoSuper_nolock(%rip), %rdx   // rdx is not used, so far.
    addq    $0x10, %rdx
    jmp     *%rdx
