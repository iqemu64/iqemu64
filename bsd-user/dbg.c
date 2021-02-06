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

#include "qemu.h"
#include <dlfcn.h>
#include <libunwind.h>

unw_context_t dbguc;
uint64_t dbgucs[128]; // unwind callstack
int dbgdepth;

#define dbgf(...) fprintf(stderr, __VA_ARGS__)

//
// These functions are presented to lldb as helpers

#define DECLARE_DBG_PRINT_XREG(x)            \
uint64_t print_x##x() {                 \
    CPUState *cpu = thread_cpu;         \
    CPUArchState *env = cpu->env_ptr;   \
    return env->xregs[x];               \
}

DECLARE_DBG_PRINT_XREG(0);
DECLARE_DBG_PRINT_XREG(1);
DECLARE_DBG_PRINT_XREG(2);
DECLARE_DBG_PRINT_XREG(3);
DECLARE_DBG_PRINT_XREG(4);
DECLARE_DBG_PRINT_XREG(5);
DECLARE_DBG_PRINT_XREG(6);
DECLARE_DBG_PRINT_XREG(7);
DECLARE_DBG_PRINT_XREG(8);
DECLARE_DBG_PRINT_XREG(9);
DECLARE_DBG_PRINT_XREG(10);
DECLARE_DBG_PRINT_XREG(11);
DECLARE_DBG_PRINT_XREG(12);
DECLARE_DBG_PRINT_XREG(13);
DECLARE_DBG_PRINT_XREG(14);
DECLARE_DBG_PRINT_XREG(15);
DECLARE_DBG_PRINT_XREG(16);
DECLARE_DBG_PRINT_XREG(17);
DECLARE_DBG_PRINT_XREG(18);
DECLARE_DBG_PRINT_XREG(19);
DECLARE_DBG_PRINT_XREG(20);
DECLARE_DBG_PRINT_XREG(21);
DECLARE_DBG_PRINT_XREG(22);
DECLARE_DBG_PRINT_XREG(23);
DECLARE_DBG_PRINT_XREG(24);
DECLARE_DBG_PRINT_XREG(25);
DECLARE_DBG_PRINT_XREG(26);
DECLARE_DBG_PRINT_XREG(27);
DECLARE_DBG_PRINT_XREG(28);
DECLARE_DBG_PRINT_XREG(29);
DECLARE_DBG_PRINT_XREG(30);
DECLARE_DBG_PRINT_XREG(31);

uint64_t get_sig_handler(int signum)
{
    struct sigaction sigactions;
    sigaction(signum, NULL, &sigactions);
    return (uint64_t)sigactions.__sigaction_u.__sa_handler;
}

uint64_t print_lr()
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    return env->xregs[30];
}

uint64_t print_pc()
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    return env->pc;
}

uint64_t print_sp()
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    return env->xregs[31];
}

uint64_t print_env()
{
    CPUState *cpu = thread_cpu;
    return (uint64_t)cpu->env_ptr;
}


// set breakpoint by arm64 pc. Check macro DBG_RECORD_ASM_PAIR
GHashTable *dbg_in_out;
GHashTable *dbg_pending_bps;
target_ulong dbg_in;
target_ulong dbg_out; // only used by lldb debugger

void dbg_add_pending_bps(uint64_t loc) {
    g_hash_table_add(dbg_pending_bps, (gpointer)loc);
}

// The second parameter is used by lldb debugger. See dbg.py
void dbg_remove_pending_bps(uint64_t in, uint64_t out) {
    g_hash_table_remove(dbg_pending_bps, (gconstpointer)in);
}

void dbg_get_inout_pair(uint64_t in, uint64_t *out) {
    *out = (uint64_t)g_hash_table_lookup(dbg_in_out, (gconstpointer)in);
}

/*
 High-overhead counter-based Profiler

 The master switch, variable `dbg_record`, is initialized to false.  Set it to
 true in lldb whenever you want to start to record.

 usage:
    (lldb) p dbg_record=1
    (lldb) c
    the next time you interrupt the process, run
    (lldb) p print_callstack()
    it shows each frame with format (dynamic_addr, [offset_to_lib, libname])
 */
bool dbg_record;

// use dbg_statistics_t as { pc, cnt }
GHashTable *dbg_pc_cnt;

guint pccnt_hash(gconstpointer _val) {
    dbg_statistics_t *st = (dbg_statistics_t *)_val;
    return g_direct_hash((gconstpointer)st->data);
}

gboolean pccnt_eq(gconstpointer a, gconstpointer b) {
    dbg_statistics_t *pa = (dbg_statistics_t *)a;
    dbg_statistics_t *pb = (dbg_statistics_t *)b;
    return (pa->data == pb->data);
}

#define pccnt_key_free g_free

// for sorting
gint pccnt_cmp(gconstpointer a, gconstpointer b) {
    dbg_statistics_t *pa = (dbg_statistics_t *)a;
    dbg_statistics_t *pb = (dbg_statistics_t *)b;
    
    if (pa->cnt < pb->cnt)
        return -1;
    else if (pa->cnt > pb->cnt)
        return 1;
    return 0;
}

static void print_pc_cnt_internal(gpointer data, gpointer limit)
{
    dbg_statistics_t *st = (dbg_statistics_t *)data;
    uint64_t pc = (uint64_t)st->data;
    uint64_t cnt = st->cnt;
    
    if (cnt < (uint64_t)limit)
        return;
    
    dbgf("%9lld cnt ", cnt);
    if (need_emulation(pc)) {
        uint64_t offset;
        const char *name;
        dbg_solve_symbol_name(pc, NULL, &offset, &name);
        dbgf("0x%llx 0x%llx %s\n", pc, offset, strrchr(name, '/'));
    } else {
        dbgf("0x%llx\n", pc);
    }
}

void print_pc_cnt()
{
    GList *list = g_hash_table_get_keys(dbg_pc_cnt);
    list = g_list_sort(list, pccnt_cmp);
    g_list_foreach(list, print_pc_cnt_internal, (gpointer)100);
    g_list_free(list);
}

void clear_pc_cnt()
{
    g_hash_table_remove_all(dbg_pc_cnt);
}

// use dbg_statistics_t as { callstack_ptr, cnt }
GHashTable *dbg_callstack;

guint callstack_hash(gconstpointer _val) {
    dbg_statistics_t *st = (dbg_statistics_t *)_val;
    uint64_t *cs = st->data;
    uint64_t sum = 0;
    for (int i = 0; i < DBG_CALLSTACK_DEPTH; ++i) {
        sum += cs[i];
    }
    return g_direct_hash((gconstpointer)sum);
}

gboolean callstack_eq(gconstpointer a, gconstpointer b) {
    dbg_statistics_t *stpa = (dbg_statistics_t *)a;
    dbg_statistics_t *stpb = (dbg_statistics_t *)b;
    
    uint64_t *pa = stpa->data;
    uint64_t *pb = stpb->data;
    for (int i = 0; i < DBG_CALLSTACK_DEPTH; ++i) {
        if (pa[i] != pb[i])
            return false;
    }
    return true;
}

void callstack_key_free(gpointer p) {
    dbg_statistics_t *st = (dbg_statistics_t *)p;
    g_free(st->data);
    g_free(p);
}

#define callstack_cmp pccnt_cmp

static void print_callstack_internal(gpointer data, gpointer limit) {
    dbg_statistics_t *st = (dbg_statistics_t *)data;
    uint64_t cnt = st->cnt;
    
    if (cnt < (uint64_t)limit)
        return;
    
    uint64_t *callstack = st->data;
    dbgf("%9lld cnt, stack:\n", (uint64_t)cnt);
    for (int i = 0; i < DBG_CALLSTACK_DEPTH; ++i) {
        uint64_t pc = callstack[i];
        if (pc == 0)
            break;
        
        if (need_emulation(pc)) {
            uint64_t offset;
            const char *name;
            dbg_solve_symbol_name(pc, NULL, &offset, &name);
            dbgf("0x%llx 0x%llx %s\n", pc, offset, strrchr(name, '/'));
        } else {
            dbgf("0x%llx\n", pc);
        }
    }
    dbgf("\n");
}

void print_callstack() {
    GList *list = g_hash_table_get_keys(dbg_callstack);
    list = g_list_sort(list, callstack_cmp);
    g_list_foreach(list, print_callstack_internal, (gpointer)100);
    g_list_free(list);
}

void clear_callstack() {
    g_hash_table_remove_all(dbg_callstack);
}

bool dbg_print_x64_on;
void print_func_name(const char *name) {
    if (!dbg_print_x64_on)
        return;
    
    dbgf("-- calling: %s\n", name);
}

void init_dbg_helper()
{
    dbg_record = false;
    // hash set
    dbg_pc_cnt = g_hash_table_new_full(pccnt_hash, pccnt_eq, pccnt_key_free, NULL);
    // hash set
    dbg_callstack = g_hash_table_new_full(callstack_hash, callstack_eq, callstack_key_free, NULL);
    
    dbg_in_out = g_hash_table_new(NULL, NULL);
    // hash set
    dbg_pending_bps = g_hash_table_new(NULL, NULL);
    
    dbg_print_x64_on = false;
}

// Called with mmap_lock held.
void dbg_do_record(target_ulong pc) {
    if (!dbg_record)
        return;
    
#if DEBUG == 1
    dbg_statistics_t *orig_st;
    dbg_statistics_t tmp;
    
    // pc cnt
    tmp.data = (uint64_t *)pc;
    
    orig_st = g_hash_table_lookup(dbg_pc_cnt, (gconstpointer)&tmp);
    if (orig_st) {
        orig_st->cnt = orig_st->cnt + 1;
    } else {
        dbg_statistics_t *st = g_new(dbg_statistics_t, 1);
        st->data = (uint64_t *)pc;
        st->cnt = 1;
        g_hash_table_add(dbg_pc_cnt, (gpointer)st);
    }
    
    // callstack
    uint64_t cs[DBG_CALLSTACK_DEPTH];
    tmp.data = cs;
    
    unw_context_t uc;
    unw_getcontext(&uc);
    _Unwind_Reason_Code code = _Unwind_CallStack2(&uc, tmp.data, DBG_CALLSTACK_DEPTH);
    if (code == _URC_END_OF_STACK) {
        orig_st = g_hash_table_lookup(dbg_callstack, (gconstpointer)&tmp);
        if (orig_st) {
            orig_st->cnt = orig_st->cnt + 1;
        } else {
            dbg_statistics_t *st = g_new(dbg_statistics_t, 1);
            size_t size = sizeof(uint64_t) * DBG_CALLSTACK_DEPTH;
            st->data = (uint64_t *)g_malloc0(size);
            memcpy(st->data, tmp.data, size);
            st->cnt = 1;
            g_hash_table_add(dbg_callstack, (gpointer)st);
        }
    }
#endif
}

// End debug helpers
//

const char *
dbg_solve_symbol_name(uintptr_t addr,
                      uint64_t *offset_to_symbol,
                      uint64_t *offset_to_lib,
                      const char **libname)
{
    Dl_info info;
    if(libname)
        *libname = NULL;
    if(offset_to_symbol)
        *offset_to_symbol = 0;
    if(offset_to_lib)
        *offset_to_lib = 0;
    
    if(dladdr((const void *)addr, &info)) {
        if(offset_to_symbol)
            *offset_to_symbol = addr - (uintptr_t)info.dli_saddr;
        if(offset_to_lib)
            *offset_to_lib = addr - (uintptr_t)info.dli_fbase;
        if(libname)
            *libname = info.dli_fname;
        return info.dli_sname;
    }
    return NULL;
}

static
const char *
get_libname_from_full_path(const char *fullpath)
{
    size_t l = strlen(fullpath);

    for(size_t i = l - 1; i != 0; i --) {
        if(fullpath[i] == '/')
            return fullpath + i + 1;
    }
    return fullpath;
}

void
dbg_on_code_gen_jit(target_ulong pc)
{
    uint64_t offset_to_lib;
    const char *libname;
    dbg_solve_symbol_name(pc, NULL, &offset_to_lib, &libname);
    fprintf(stderr, "tb_code_gen: 0x%llx from %s\n", offset_to_lib, get_libname_from_full_path(libname));
}

void
dbg_on_pc_branch_search(target_ulong pc)
{
    uint64_t offset_to_lib;
    const char *libname;
    dbg_solve_symbol_name(pc, NULL, &offset_to_lib, &libname);
    fprintf(stderr, "new pc: 0x%llx from %s\n", offset_to_lib, get_libname_from_full_path(libname));
}
