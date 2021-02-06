/*
 *  qemu user main
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2020 上海芯竹科技有限公司
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/units.h"
#include "sysemu/tcg.h"
#include "qemu-version.h"

#include "qapi/error.h"
#include "qemu.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/path.h"
#include "qemu/help_option.h"
#include "qemu/module.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "qemu/timer.h"
#include "qemu/envlist.h"
#include "exec/log.h"
#include "trace/control.h"
#include "errno-defs.h"

#include <dlfcn.h>
#include <mach/mach_init.h>
#include <mach/task.h>
#include <mach/task_policy.h>
#include <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <mach-o/dyld.h>

#define EXCP_DUMP(env, fmt, ...)                                        \
do {                                                                    \
    CPUState *cs = env_cpu(env);                                        \
    fprintf(stderr, fmt , ## __VA_ARGS__);                              \
    cpu_dump_state(cs, stderr, 0);                                      \
    if (qemu_log_separate()) {                                          \
        qemu_log(fmt, ## __VA_ARGS__);                                  \
        log_cpu_state(cs, 0);                                           \
    }                                                                   \
} while (0)

int singlestep;
unsigned long mmap_min_addr;
unsigned long guest_base;
int have_guest_base;
unsigned long reserved_va;

static const char *interp_prefix = CONFIG_QEMU_INTERP_PREFIX;
const char *qemu_uname_release;
extern char **environ;
enum BSDType bsd_type;

/* XXX: iOS stack space is 1MB for main thread and 512kb for
        secondary threads. But we are using 1MB in all cases. */
const unsigned long guest_stack_size = 1 * 1024 * 1024UL;

static const char *cpu_type;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void fork_start(void)
{
}

void fork_end(int child)
{
    if (child) {
        gdbserver_fork(thread_cpu);
    }
}

THREAD CPUState *thread_cpu;
THREAD TaskState thread_ts;
static target_ulong pc_page_size = 0, pc_page_base = 0;
static target_ulong pc_linkedit_size = 0, pc_linkedit_base = 0;

void prepare_env(CPUArchState *env, struct xloop_param *param)
{
    env->xregs[30] = *(uint64_t *)param->unwind_context->rsp;
    if(env->xregs[31] == 0) {
        /* virgin. map stack for it. */
        abi_ulong error = target_mmap(0, guest_stack_size + TARGET_PAGE_SIZE, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (error == -1) {
            perror("mmap stack");
            abort();
        }
        
        env->stack_top = error;
        
        /* We reserve one extra page at the top of the stack as guard.  */
        target_mprotect(error, TARGET_PAGE_SIZE, PROT_NONE);
        env->xregs[31] = error + guest_stack_size + TARGET_PAGE_SIZE;
        env->stack_bottom = env->xregs[31];
    }
}

void
CFIList_Truncate_To_By_Count(CPUArchState *env, int count)
{
    if(count > 0) {
        CFIEntry *pcfi = QTAILQ_FIRST(&env->CFIhead);
        while(count-- > 0) {
            QTAILQ_REMOVE(&env->CFIhead, pcfi, link);
            pcfi = QTAILQ_FIRST(&env->CFIhead);
        }
        if(pcfi) {
            env->xregs[30] = *pcfi->p_arm64_pc;
            env->xregs[29] = *pcfi->p_arm64_fp;
            env->xregs[31] = *pcfi->p_arm64_sp;
            
            pcfi->p_arm64_pc = &env->xregs[30];
            pcfi->p_arm64_fp = &env->xregs[29];
            pcfi->p_arm64_sp = &env->xregs[31];
        }
    }
}

void
CFIList_Truncate_To(CPUArchState *env, CFIEntry *entry)
{
    //
    // set new CFI head.
    // we could have just set new head, but there is no macro for this.
    // crap.
    CFIEntry *pcfi = QTAILQ_FIRST(&env->CFIhead);
    while(pcfi != entry) {
        QTAILQ_REMOVE(&env->CFIhead, pcfi, link);
        pcfi = QTAILQ_FIRST(&env->CFIhead);
    }
    
    entry->p_arm64_pc = &env->xregs[30];
    entry->p_arm64_fp = &env->xregs[29];
    entry->p_arm64_sp = &env->xregs[31];
}

void
CFIList_Insert_Head(CPUArchState *env, CFIEntry *entry,
                     uint64_t *last_pc,
                     uint64_t *last_fp,
                     uint64_t *last_sp)
{
    /* setup last CFI, unlink registers from the env and link it to the saved ones */
    CFIEntry *plast_cfi = QTAILQ_FIRST(&env->CFIhead);
    if(plast_cfi) {
        plast_cfi->p_arm64_pc = last_pc;
        plast_cfi->p_arm64_fp = last_fp;
        plast_cfi->p_arm64_sp = last_sp;
    }
    entry->p_arm64_pc = &env->xregs[30];
    entry->p_arm64_fp = &env->xregs[29];
    entry->p_arm64_sp = &env->xregs[31];
    
    QTAILQ_INSERT_HEAD(&env->CFIhead, (struct CFIEntry *)entry, link);
}

__attribute__((force_align_arg_pointer))
void cpu_xloop() {
    
    CPUState *cs = thread_cpu;
    CPUARMState *env;
    
    // paste begins
        
    if(NULL == cs) {
        /* a new thread is being created */
        env = create_arch_cpu();
        cs = env_cpu(env);
        thread_cpu = cs;
    } else {
        env = cs->env_ptr;
    }
        
    env = cs->env_ptr;
    volatile int trapnr = 0, backfromhell = 0;
    volatile uint64_t saved_lr = 0;
    uint64_t saved_sp = 0, saved_fp = 0;
    
    target_siginfo_t info;
    volatile CFIEntry cfi;
    
    volatile fnEntryTranslation entry_translation = NULL;
    volatile fnExitTranslation exit_translation = NULL;
    
    /* we make a copy of xloop_param in the stack */
    volatile struct xloop_param xloop_param;
    memcpy((void *)&xloop_param, &tls_xloop_param, sizeof(xloop_param));

    /* to figure out where are we */
    
    if(0 == pc_page_base) {
        get_address_map((const void *)env->pc,
                        &pc_page_base, &pc_page_size, &pc_linkedit_base, &pc_linkedit_size);
    }
    
    /*
     * Need to set context for current execution.
     * Including stack mapping, pc setting and type interpretation.
     * The return value is the stack size we need to prepare in
     * the new arm function parameter calling.
     * Param: extra_param
     *  In 32-bit:
     *      When the return value of a function is a structure, arm
     *      ABI always uses a struct return while i386 does it as
     *      appropriate. When arm uses struct return and i386 doesn't,
     *      arm code will seek to write into a stack space in the
     *      first parameter that doesn't exist from the i386 caller.
     *      This is when extra_param comes in, it provides the space
     *      arm code needs to write to. It has a maximum of 8 bytes,
     *      because when it's greater than 8 bytes, i386 also uses
     *      a struct return, the caller will provide the space from
     *      the compile time.
     *  In 64-bit:
     *      TODO: Is it essential anymore?
     */
    // Old usages. To be removed.
    //uint8_t extra_param[8];
    //prepare_env(env, &xloop_param, extra_param);
    saved_lr = env->xregs[30];      // LR is a goddamn caller-saved register,
                                    // so we must stand out to do the save ourself.
    saved_fp = env->xregs[29];
    saved_sp = env->xregs[31];      // fp & sp are safely restored in the return
                                    // of cpu_xloop, we just need a copy of them
                                    // to be referenced.
    
    prepare_env(env, (struct xloop_param *)&xloop_param);
    if(xloop_param.entry_translation == NULL ||
       xloop_param.exit_translation == NULL) {
        // determine it by pc
        abi_x2a_get_translation_function_pair_by_pc(env->pc, (fnEntryTranslation *)&entry_translation, (fnExitTranslation *)&exit_translation);
    } else {
        entry_translation = xloop_param.entry_translation;
        exit_translation = xloop_param.exit_translation;
        // consume tls_xloop_param translation pair
        tls_xloop_param.entry_translation = NULL;
        tls_xloop_param.exit_translation = NULL;
    }
    
    if(!entry_translation || !exit_translation) {
        qemu_log("Translation not found for pc 0x%llx\n",
                 env->pc);
        abort();
    }
    
    entry_translation(env, xloop_param.unwind_context);

    
    /* setup CFI for this frame. */
    
    cfi.x64_pc = *(uintptr_t *)xloop_param.unwind_context->rsp;
    cfi.x64_sp = xloop_param.unwind_context->rsp;
    cfi.x64_fp = xloop_param.unwind_context->rbp;
    CFIList_Insert_Head(env, (CFIEntry *)&cfi, (uint64_t *)&saved_lr, &saved_fp, &saved_sp);
    
    sigjmp_buf saved_jmpbuf;
    memcpy(saved_jmpbuf, cs->jmp_env, sizeof(saved_jmpbuf));
    if(0 == setjmp((int *)cfi.jbuf)) {
        // do nothing.
    } else {
        //abort();
        trapnr = 0;
        backfromhell = 1;
        
        // Technically it is not safe to use head, because after a
        // longjmp, the stack that stores the head is no longer safe.
        // But since no code has used that space yet, we set the new
        // head with luck.
        CFIList_Truncate_To(env, (CFIEntry *)&cfi);
        
        fprintf(stderr, "[%lld] back from hell, new pc: %08llx.\n", cs->thread_id, env->pc);
    }
    
    abi_long ret;

    for (;;) {
        if(backfromhell)
            backfromhell = 0;
        
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        
        env->arm_errno = errno;
        
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SWI:
            ret = do_syscall(env,
                             env->xregs[8],
                             env->xregs[0],
                             env->xregs[1],
                             env->xregs[2],
                             env->xregs[3],
                             env->xregs[4],
                             env->xregs[5],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->xregs[0] = ret;
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_UDEF:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            /* XXX: check env->error_code */
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = env->exception.vaddress;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_DEBUG:
        case EXCP_BKPT:
            info.si_signo = TARGET_SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_TRAP_BRKPT;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_SEMIHOST:
            env->xregs[0] = do_arm_semihosting(env);
            env->pc += 4;
            break;
        case EXCP_YIELD:
            /* nothing to do here for user-mode, just resume guest code */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_XLOOP_FINISH: {
            /* copy the retn stack count to the real xloop_param */
            tls_xloop_param.retn_stack_count = xloop_param.retn_stack_count;
            
            QTAILQ_REMOVE(&env->CFIhead, &cfi, link);
            memcpy(cs->jmp_env, saved_jmpbuf, sizeof(saved_jmpbuf));
            
            CFIEntry *plast_cfi = QTAILQ_FIRST(&env->CFIhead);
            if(plast_cfi) {
                plast_cfi->p_arm64_pc = &env->xregs[30];
                plast_cfi->p_arm64_fp = &env->xregs[29];
                plast_cfi->p_arm64_sp = &env->xregs[31];
            }

            exit_translation(env, xloop_param.unwind_context);
            env->xregs[30] = saved_lr;
            return;
        }
        default:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
        errno = env->arm_errno;
        /* Exception return on AArch64 always clears the exclusive monitor,
         * so any return to running guest code implies this.
         */
        env->exclusive_addr = -1;
    }
    
}

bool qemu_cpu_is_self(CPUState *cpu)
{
    return thread_cpu == cpu;
}

void qemu_cpu_kick(CPUState *cpu)
{
    cpu_exit(cpu);
}

CPUArchState *create_arch_cpu()
{
    mmap_lock();
    CPUState *cpu = cpu_create(cpu_type);
    
    if(!cpu) {
        qemu_log("Unable to create new thread env\n");
        mmap_unlock();
        exit(1);
    }
    
    CPUArchState *env = cpu->env_ptr;
    cpu_reset(cpu);
    
    QTAILQ_INIT(&env->CFIhead);
    
    /* build Task State */
    TaskState *ts = &thread_ts; // g_new0(TaskState, 1);
    memset(ts, 0, sizeof(TaskState));
    init_task_state(ts);
    ts->info = NULL;
    cpu->opaque = ts;
    
    /* init tls_xloop_param translation pair */
    tls_xloop_param.entry_translation = NULL;
    tls_xloop_param.exit_translation = NULL;
    
    mmap_unlock();
    return env;
}

/* Assumes contents are already zeroed.  */
void init_task_state(TaskState *ts)
{
    ts->used = 1;
    ts->sigaltstack_used = (struct target_sigaltstack) {
        .ss_sp = 0,
        .ss_size = 0,
        .ss_flags = TARGET_SS_DISABLE,
    };
}

static
void
image_preload(const struct mach_header *mh,
              intptr_t vmaddr_slide)
{
    if(is_image_foreign((const struct MACH_HEADER *)mh)) {
        loader_exec((const struct MACH_HEADER *)mh);
        app_compatibility_level((const struct MACH_HEADER *)mh, vmaddr_slide);
    }
}

static __attribute__((constructor, visibility("default"), used))
void
firstborn_init()
{
    hook_self_malloc();
}

static __attribute__((constructor, visibility("default"), used))
void
libiqemu_init(void)
{
    const char *cpu_model;
    const char *log_file = NULL;
    const char *log_mask = NULL;
    struct image_info info1, *info = &info1;
    TaskState *ts;
    CPUArchState *env;
    CPUState *cpu;
    int gdbstub_port = 0;
    char **target_environ, **wrk;
    envlist_t *envlist = NULL;
    char *trace_file = NULL;
    bsd_type = target_openbsd;

    error_init("libiqemu");
    module_call_init(MODULE_INIT_TRACE);
    qemu_init_cpu_list();
    module_call_init(MODULE_INIT_QOM);

    envlist = envlist_create();

    /* add current environment into the list */
    for (wrk = environ; *wrk != NULL; wrk++) {
        (void) envlist_setenv(envlist, *wrk);
    }

    cpu_model = NULL;

    qemu_add_opts(&qemu_trace_opts);

    /* init debug */
    qemu_log_needs_buffers();
    qemu_set_log_filename(log_file, &error_fatal);
    if (log_mask) {
        int mask;

        mask = qemu_str_to_log_mask(log_mask);
        if (!mask) {
            qemu_print_log_usage(stdout);
            exit(1);
        }
        qemu_set_log(mask);
    }
    
#if DEBUG == 1
    stderr->_write = RedirectOutputToView;
    stdout->_write = RedirectOutputToView;
#endif

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    cpu_model = "any";

    /* init tcg before creating CPUs and to get qemu_host_page_size */
    tcg_exec_init(0);

    cpu_type = parse_cpu_option(cpu_model);
    cpu = cpu_create(cpu_type);
    env = cpu->env_ptr;

    cpu_reset(cpu);

    thread_cpu = cpu;

    if (getenv("QEMU_STRACE")) {
        do_strace = 1;
    }

    target_environ = envlist_to_environ(envlist, NULL);
    envlist_free(envlist);

    /*
     * Now that page sizes are configured in tcg_exec_init() we can do
     * proper page alignment for guest_base.
     */
    guest_base = HOST_PAGE_ALIGN(guest_base);

    for (wrk = target_environ; *wrk; wrk++) {
        g_free(*wrk);
    }

    g_free(target_environ);

    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        qemu_log("guest_base  0x%lx\n", guest_base);
        log_page_dump("binary load");

        qemu_log("start_brk   0x" TARGET_ABI_FMT_lx "\n", info->start_brk);
        qemu_log("end_code    0x" TARGET_ABI_FMT_lx "\n", info->end_code);
        qemu_log("start_code  0x" TARGET_ABI_FMT_lx "\n",
                 info->start_code);
        qemu_log("start_data  0x" TARGET_ABI_FMT_lx "\n",
                 info->start_data);
        qemu_log("end_data    0x" TARGET_ABI_FMT_lx "\n", info->end_data);
        qemu_log("start_stack 0x" TARGET_ABI_FMT_lx "\n",
                 info->start_stack);
        qemu_log("brk         0x" TARGET_ABI_FMT_lx "\n", info->brk);
        qemu_log("entry       0x" TARGET_ABI_FMT_lx "\n", info->entry);
    }

    target_set_brk(info->brk);
    syscall_init();
    signal_init();

    /* Now that we've loaded the binary, GUEST_BASE is fixed.  Delay
       generating the prologue until now so that the prologue can take
       the real value of GUEST_BASE into account.  */
    tcg_prologue_init(tcg_ctx);
    tcg_region_init();

    /* build Task State */
    ts = g_new0(TaskState, 1);
    memset(ts, 0, sizeof(TaskState));
    init_task_state(ts);
    ts->info = info;
    cpu->opaque = ts;

    if (gdbstub_port) {
        gdbserver_start (gdbstub_port);
        gdb_handlesig(cpu, 0);
    }
    
    /* Custom initailizations begin here. */
    
    /* chdir */
    Dl_info dlinfo;
    char mainexepath[512];
    void *sym = dlsym(RTLD_MAIN_ONLY, "_mh_execute_header");
    dladdr(sym, &dlinfo);
    size_t l = strlen(dlinfo.dli_fname);
    memcpy(mainexepath, dlinfo.dli_fname, l + 0);
    for(size_t i = l - 1; i != 0; i --) {
        if(mainexepath[i] == '/' ||
           mainexepath[i] == '\\') {
            mainexepath[i] = '\0';
            break;
        }
    }
    chdir(mainexepath);
    
    /* rebuild some of the ENVs */
    
    char *long_home = getenv("CFFIXED_USER_HOME");
    char short_home[512];
    realpath(long_home, short_home);
    setenv("CFFIXED_USER_HOME", short_home, 1);
    
    /* check out priority */
    task_category_policy_data_t category_policy;
    kern_return_t result;
    
    category_policy.role = TASK_FOREGROUND_APPLICATION;
    result = task_policy_set(mach_task_self(), TASK_CATEGORY_POLICY,
                             (task_policy_t)(&category_policy),
                             TASK_CATEGORY_POLICY_COUNT);
    if(result != KERN_SUCCESS) {
        qemu_log("task_policy_set failed.\n");
        abort();
    }
    
    init_prototype_system();
    if(!init_dyld_map()) {
        qemu_log("init_dyld_map() error.");
        abort();
    }
    _dyld_register_func_for_add_image(image_preload);

    dladdr(objc_getClass, &dlinfo);
    init_objc_system((struct MACH_HEADER *)dlinfo.dli_fbase);
    
    dladdr(dispatch_sync, &dlinfo);
    init_block_system((struct MACH_HEADER *)dlinfo.dli_fbase);
    
    init_dbg_helper();
    register_important_funcs();
}
