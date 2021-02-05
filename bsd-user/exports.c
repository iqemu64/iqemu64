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
bool        iqemu_get_CFI(int index, uintptr_t *sp, uintptr_t *fp)
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
    
    *sp = cfi->sp;
    *fp = cfi->fp;
    return true;
}

EXPORT
void        iqemu_set_xloop_params_by_types(const char *types)
{
    abi_x2a_get_translation_function_pari_by_types(types, &tls_xloop_param.entry_translation, &tls_xloop_param.exit_translation);
}

EXPORT __attribute__((naked))
void        __iqemu_begin_emulation(void)
{
    __asm__ __volatile__("movq _tcg_ctx(%%rip), %%r10\n"
             "movq %c[OffsetOfA](%%r10), %%r10\n"
             "jmp *%%r10\n"
             :
             : [OffsetOfA] "i" (offsetof(TCGContext, code_gen_xloop_trampoline)));
}
