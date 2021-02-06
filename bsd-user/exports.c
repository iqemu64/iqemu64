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

#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "qemu.h"
#include "exports.h"
#include "tcg/tcg.h"
#include "exec/exec-all.h"

#define EXPORT __attribute__((visibility("default")))

EXPORT
bool        iqemu_is_code_in_jit(uintptr_t v)
{
    if(!tcg_ctx) return false;
    return  v >= (uintptr_t)tcg_ctx->code_gen_buffer &&
            v < (uintptr_t)tcg_ctx->code_gen_buffer + tcg_ctx->code_gen_buffer_size;
}

EXPORT
int *       iqemu_get_jmp_buf(int index)
{
    CPUArchState *env = thread_cpu->env_ptr;
    CFIEntry *cfi = QTAILQ_FIRST(&env->CFIhead);
    
    while(index > 0 && cfi) {
        index --;
        cfi = QTAILQ_NEXT(cfi, link);
    }
    
    if(NULL == cfi) {
        return NULL;
    }
    
    return cfi->jbuf;
}

EXPORT
int         iqemu_truncate_CFI(int index)
{
    if(thread_cpu) {
        CPUArchState *env = thread_cpu->env_ptr;
        if(env)
            CFIList_Truncate_To_By_Count(env, index);
    }
    return 0;
}

EXPORT
void *      iqemu_get_current_thread_context(void)
{
    return thread_cpu;
}

EXPORT
bool        iqemu_need_emulation(uintptr_t v)
{
    return need_emulation(v);
}

EXPORT
uintptr_t   iqemu_get_context_register(void *context, int num)
{
    CPUArchState *env = ((CPUState *)context)->env_ptr;
    if(num == IQEMU_REG_PC) {
        return env->pc;
    } else if(num == IQEMU_REG_CPSR) {
        return cpsr_read(env);
    } else {
        if(num > IQEMU_REG_SP) {
            // This is not a valid register.
            abort();
        }
        return env->xregs[num - IQEMU_REG_X0];
    }
}

EXPORT
void        iqemu_set_context_register(void *context, int num, uintptr_t value)
{
    CPUArchState *env = ((CPUState *)context)->env_ptr;
    if(num == IQEMU_REG_PC) {
        env->pc = value;
    } else if(num == IQEMU_REG_CPSR) {
        cpsr_write(env, (uint32_t)value, CPSR_USER, CPSRWriteRaw);
    } else {
        if(num > IQEMU_REG_SP) {
            // This is not a valid register.
            abort();
        }
        env->xregs[num - IQEMU_REG_X0] = value;
    }
}

EXPORT
bool        iqemu_get_x64_CFI(int index, uintptr_t *sp, uintptr_t *fp)
{
    CPUArchState *env = thread_cpu->env_ptr;
    CFIEntry *cfi = QTAILQ_FIRST(&env->CFIhead);
    *sp = 0;
    *fp = 0;
    
    while(index > 0 && cfi) {
        index --;
        cfi = QTAILQ_NEXT(cfi, link);
    }
    
    if(NULL == cfi)
        return false;
    
    *sp = cfi->x64_sp;
    *fp = cfi->x64_fp;
    return true;
}

EXPORT
bool        iqemu_get_arm64_CFI(int index, uintptr_t *sp, uintptr_t *fp, uintptr_t *pc)
{
    CPUArchState *env = thread_cpu->env_ptr;
    CFIEntry *cfi = QTAILQ_FIRST(&env->CFIhead);
    *sp = 0;
    *fp = 0;
    *pc = 0;
    
    while(index > 0 && cfi) {
        index --;
        cfi = QTAILQ_NEXT(cfi, link);
    }
    
    if(NULL == cfi)
        return false;
    
    *sp = *cfi->p_arm64_sp;
    *fp = *cfi->p_arm64_fp;
    *pc = *cfi->p_arm64_pc;
    return true;
}

EXPORT
void        iqemu_set_xloop_params_by_types(const char *types)
{
    abi_x2a_get_translation_function_pari_by_types(types, &tls_xloop_param.entry_translation, &tls_xloop_param.exit_translation);
}

uintptr_t xloop_pc;

EXPORT
void        iqemu_set_xloop_pc(uintptr_t pc)
{
    xloop_pc = pc;
}

// check tcg_ctx->code_gen_xloop_trampoline and cpu_xloop_helper() before you
// modify the following asm
EXPORT __attribute__((naked))
void        __iqemu_begin_emulation(void)
{
    __asm__ __volatile__(
        "movq _xloop_pc(%%rip), %%r11\n"
        "push %%r11\n"
        "movq _tcg_ctx(%%rip), %%r10\n"
        "movq %c[OffsetOfA](%%r10), %%r10\n"
        "jmp *%%r10\n"
        :
        : [OffsetOfA] "i" (offsetof(TCGContext, code_gen_xloop_trampoline))
    );
}
