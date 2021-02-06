/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (c) 2020 上海芯竹科技有限公司
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef EXEC_TB_LOOKUP_H
#define EXEC_TB_LOOKUP_H

#ifdef NEED_CPU_H
#include "cpu.h"
#else
#include "exec/poison.h"
#endif

#include "exec/exec-all.h"
#include "exec/tb-hash.h"

#include "dbg.h"
#include "exports.h"
#include "objc/objc.h"

/* Might cause an exception, so have a longjmp destination ready */
static inline TranslationBlock *
tb_lookup__cpu_state(CPUState *cpu, target_ulong *pc)
{
    CPUArchState *env = (CPUArchState *)cpu->env_ptr;
    TranslationBlock *tb;
    uint32_t hash;

    cpu_get_tb_cpu_state(env, pc);
    // Get the real pc if it is already in jit code
    if(iqemu_is_code_in_jit(*pc)) {
        // real one
        if(*pc != QTAILQ_FIRST(&env->CFIhead)->x64_pc) {
            uint32_t mark = objc_get_real_pc((uintptr_t *)pc, NULL);
            if(mark == OBJC_ARM_MARK || mark == OBJC_ARM_BLOCK_MARK) {
                // correct
            } else if (mark == CALLBACK_TRAMPOLINE_MARK) {
                // correct
            } else {
                // TODO: Unknown situation, what we do?
                abort();
            }
        }
    }
    hash = tb_jmp_cache_hash_func(*pc);
    tb = atomic_rcu_read(&cpu->tb_jmp_cache[hash]);

    if (likely(tb &&
               tb->pc == *pc)) {
#if defined(QEMU_CORE_DEBUG_ON) && defined(QEMU_CORE_DEBUG_PC_BRANCH_SEARCH)
        dbg_on_pc_branch_search(*pc);
#endif
        return tb;
    }
    rcu_read_lock();
    tb = tb_htable_lookup(cpu, *pc);
    rcu_read_unlock();
    
    if (tb == NULL) {
        return NULL;
    }
    atomic_set(&cpu->tb_jmp_cache[hash], tb);
    
#if defined(QEMU_CORE_DEBUG_ON) && defined(QEMU_CORE_DEBUG_PC_BRANCH_SEARCH)
    dbg_on_pc_branch_search(*pc);
#endif
    
    return tb;
}

#endif /* EXEC_TB_LOOKUP_H */
