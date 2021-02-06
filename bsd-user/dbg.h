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

#ifndef dbg_h
#define dbg_h

const char *
dbg_solve_symbol_name(uintptr_t addr,
                      uint64_t *offset_to_symbol,
                      uint64_t *offset_to_lib,
                      const char **libname);

// This is the overall switch.
//#define QEMU_CORE_DEBUG_ON

#define QEMU_CORE_DEBUG_CODE_GEN_JIT
void
dbg_on_code_gen_jit(target_ulong pc);

#define QEMU_CORE_DEBUG_PC_BRANCH_SEARCH
void
dbg_on_pc_branch_search(target_ulong pc);

#define DBG_RECORD_ASM_PAIR 0
#define DBG_CALLSTACK_DEPTH 9

typedef struct {
    uint64_t *data;
    uint64_t cnt;
} dbg_statistics_t;

void
init_dbg_helper(void);

#if DEBUG == 1
#include <unwind.h>
#include <libunwind.h>
extern _Unwind_Reason_Code _Unwind_CallStack2(unw_context_t*, uint64_t* callstack, int depth);
#endif

uint64_t get_sig_handler(int signum);

#endif /* dbg_h */
