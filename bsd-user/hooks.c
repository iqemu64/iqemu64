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
#include <glib.h>
#include <malloc/malloc.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include "exec/exec-all.h"
#include "exports.h"
#include "qemu/log.h"
#include <mach-o/dyld.h>
#include "tcg/tcg.h"

#define DYLD_INTERPOSE(_replacement,_replacee) \
__attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };


// Self malloc hooks
// Some apps(WeChat) uses malloc hooks. It leads to emulation
// of ARM function on top of another emulation. This is not
// allowed! Use our own version of malloc/frees to ensure
// no interruption is met during critical execution!

static malloc_zone_t *iqemu_malloc_zone;

static gpointer
iqemu_malloc(gsize n_bytes)
{
    return malloc_zone_malloc(iqemu_malloc_zone, n_bytes);
}

static gpointer
iqemu_realloc(gpointer mem,
                         gsize    n_bytes)
{
    return malloc_zone_realloc(iqemu_malloc_zone, mem, n_bytes);
}

static void
iqemu_free(gpointer mem)
{
    malloc_zone_free(iqemu_malloc_zone, mem);
}

void
hook_self_malloc()
{
    static GMemVTable vtable;
    memset(&vtable, 0, sizeof(vtable));
    
    iqemu_malloc_zone = malloc_create_zone(10000, 0);
    
    vtable.malloc = iqemu_malloc;
    vtable.realloc = iqemu_realloc;
    vtable.free = iqemu_free;
    
    g_mem_set_vtable(&vtable);
}

void call_sigaction(CPUArchState *env) {
    int signum = (int)env->xregs[0];
    const struct sigaction *act = (const struct sigaction *)env->xregs[1];
    struct sigaction * oldact = (struct sigaction *)env->xregs[2];
    const char *sa_sigaction_types = "v24i0^?8^v16";

#if DEBUG == 1
    if (signum == SIGBUS) {
        FILE *logfile = qemu_log_lock();
        qemu_log("Caught SIGBUS on sigaction!\n");
        qemu_log_unlock(logfile);
    }
#endif
    
    if (act && act->sa_handler != SIG_IGN && act->sa_handler != SIG_DFL) {
        if (need_emulation((uintptr_t)act->sa_sigaction)) {
            tcg_register_callback_as_type((target_ulong)act->sa_sigaction, sa_sigaction_types);
        }
    }
    
    struct target_sigaction ta, toa;
    struct target_sigaction *pta = NULL;
    struct target_sigaction *ptoa = NULL;
    if (act)
        pta = &ta;
    if (oldact)
        ptoa = &toa;
    
    host_to_target_sigaction(pta, act);
    host_to_target_sigaction(ptoa, oldact);
    
    env->xregs[0] = do_sigaction(signum, pta, ptoa);
    
    // act is a const pointer
    target_to_host_sigaction(oldact, ptoa);
    
    env->pc = env->xregs[30];
    // XXX: What about LR?
}

typedef void (*sighandler_t)(int);
void call_signal(CPUArchState *env) {
    int signum = (int)env->xregs[0];
    sighandler_t handler = (sighandler_t)env->xregs[1];
    
    if (signum == SIGBUS) {
        FILE *logfile = qemu_log_lock();
        qemu_log("Caught SIGBUS on signal! Disabled for now\n");
        qemu_log_unlock(logfile);
        
        env->xregs[0] = 0;
        env->pc = env->xregs[30];
        return;
    }
    
    if (handler != SIG_IGN && handler != SIG_DFL) {
        if (need_emulation((uintptr_t)handler)) {
            tcg_register_callback_as_type((target_ulong)handler, "v8i0");
        }
    }
    
    env->xregs[0] = (uint64_t)signal(signum, handler);
    
    env->pc = env->xregs[30];
}

void call_sigaltstack(CPUArchState *env) {
#if DEBUG == 1
    FILE *logfile = qemu_log_lock();
    qemu_log("sigaltstack called\n");
    qemu_log_unlock(logfile);
#endif
    const stack_t *a0 = (const stack_t *)env->xregs[0];
    stack_t *a1 = (stack_t *)env->xregs[1];
    
    env->xregs[0] = sigaltstack(a0, a1);
    
    env->pc = env->xregs[30];
}

int
mypthread_attr_setstacksize(pthread_attr_t *attr,
                            size_t size)
{
    fprintf(stderr, "pthread_attr_setstacksize: %zu\n", size);
    return 0;
}
DYLD_INTERPOSE(mypthread_attr_setstacksize, pthread_attr_setstacksize)

void call_siginterrupt(CPUArchState *env) {
#if DEBUG == 1
    FILE *logfile = qemu_log_lock();
    qemu_log("siginterrupt called\n");
    qemu_log_unlock(logfile);
#endif
    
    int a0 = (int)env->xregs[0];
    int a1 = (int)env->xregs[1];
    
    env->xregs[0] = siginterrupt(a0, a1);
    
    env->pc = env->xregs[30];
}

static pthread_mutex_t dyld_locker = PTHREAD_MUTEX_INITIALIZER;
void call_dlopen(CPUArchState *env) {
    pthread_mutex_lock(&dyld_locker);
    static void *(*dlopen_internal)(const char *path, int mode, void *callerAddress) = NULL;
    if(dlopen_internal == NULL)
        g_dyld_funcs.dyld_func_lookup("__dyld_dlopen_internal", (void **)&dlopen_internal);
    pthread_mutex_unlock(&dyld_locker);
    void *result = dlopen_internal((const char *)env->xregs[0], (int)env->xregs[1], (void *)env->xregs[30]);
    env->xregs[0] = (uint64_t)result;
    env->pc = env->xregs[30];
}

void call_dlsym(CPUArchState *env) {
    pthread_mutex_lock(&dyld_locker);
    static void *(*dlsym_internal)(void *handle, const char *symbol, void *callerAddress) = NULL;
    if(dlsym_internal == NULL)
        g_dyld_funcs.dyld_func_lookup("__dyld_dlsym_internal", (void **)&dlsym_internal);
    pthread_mutex_unlock(&dyld_locker);
    void *result = dlsym_internal((void *)env->xregs[0], (const char *)env->xregs[1], (void *)env->xregs[30]);
    if(result) {
        mmap_lock();
        if(!need_emulation_nolock((uintptr_t)result)) {
            if(!g_hash_table_lookup(tcg_ctx->addr_to_symbolname, (gpointer)result)) {
                // WARNING: symbol_name is never freed, but it is not
                // considered as a memory leak. A symbol is always there,
                // the string representing it never has to be freed.
                
                // dlsym() assumes symbolName passed in is same as in C source code
                // dyld assumes all symbol names have an underscore prefix
                const char *src = (const char *)env->xregs[1];
                size_t len = strlen(src) + 1; // '\0'
                char *symbol_name = (char *)g_malloc(1 + len); // leading '_'
                if (symbol_name) {
                    symbol_name[0] = '_';
                    memcpy(symbol_name + 1, src, len);
                }
                g_hash_table_insert(tcg_ctx->addr_to_symbolname,
                                    (gpointer)result, (gpointer)symbol_name);
            }
        }
        mmap_unlock();
    }
    env->xregs[0] = (uint64_t)result;
    env->pc = env->xregs[30];
}

//
// Interpose region
extern int class_addMethod(void * _Nullable cls, const char * _Nonnull name, void * _Nonnull imp,
                const char * _Nullable types) ;

int
myclass_addMethod(void *cls, const char *name, void *imp, const char *types) {
    const char *new_types = smoba_class_addMethod(name);
    if(!new_types) new_types = types;
    return class_addMethod(cls, name, imp, new_types);
}
DYLD_INTERPOSE(myclass_addMethod, class_addMethod)

#define IPHONE_MODEL    ("iPhone12,8")      // iPhone SE 2nd Gen
// see https://gist.github.com/adamawolf/3048717

int mysysctl(int *name, u_int types, void *buf, size_t *size, void *arg0, size_t arg1)
{
    if(name[0] == CTL_HW && name[1] == HW_MACHINE) {
        qemu_log("sysctl with HW_MACHINE called.\n");
        if(NULL == buf) {
            *size = strlen(IPHONE_MODEL) + 1;
        } else {
            if(*size > strlen(IPHONE_MODEL)) {
                strcpy(buf, IPHONE_MODEL);
            } else {
                return ENOMEM;
            }
        }
        return 0;
    } else if(types == 4) {
        if(name[0] == CTL_KERN && name[1] == KERN_PROC) {
            if(buf) {
                ((struct kinfo_proc *)buf)->kp_proc.p_flag &= (~P_TRACED);
                return 0;
            }
            *size = 0;
            return EACCES;
        }
    }
    
    return sysctl(name, types, buf, size, arg0, arg1);
}
DYLD_INTERPOSE(mysysctl, sysctl)

int mysysctlbyname(const char *name, void *buf, size_t *size, void *arg0, size_t arg1)
{
    if(name) {
        if(!strcmp(name, "hw.machine")) {
            qemu_log("sysctlbyname with hw.machine called.\n");
            
            if(NULL == buf) {
                *size = strlen(IPHONE_MODEL) + 1;
            } else {
                if(*size > strlen(IPHONE_MODEL)) {
                    strcpy(buf, IPHONE_MODEL);
                } else {
                    return ENOMEM;
                }
            }
            return 0;
        }
    }
    return sysctlbyname(name, buf, size, arg0, arg1);
}
DYLD_INTERPOSE(mysysctlbyname, sysctlbyname)

int myuname(struct utsname *name)
{
    qemu_log("uname called.\n");
    int r = uname(name);
    strcpy(name->machine, IPHONE_MODEL);
    return r;
}
DYLD_INTERPOSE(myuname, uname)

#define THREAD_STATE_FLAVOR_LIST        0
#define THREAD_STATE_FLAVOR_LIST_NEW    128
#define ARM_THREAD_STATE                1
#define ARM_VFP_STATE                   2
#define ARM_EXCEPTION_STATE             3
#define ARM_DEBUG_STATE                 4
#define ARM_THREAD_STATE64              6
#define ARM_EXCEPTION_STATE64           7
#define ARM_DEBUG_STATE64               15
#define ARM_NEON_STATE                  16
#define ARM_NEON_STATE64                17

struct arm_state_hdr {
    uint32_t flavor;
    uint32_t count;
};
typedef struct arm_state_hdr arm_state_hdr_t;

#define _STRUCT_ARM_THREAD_STATE        struct arm_thread_state
_STRUCT_ARM_THREAD_STATE
{
    uint32_t r[13];
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
    uint32_t cpsr;
};

#define _STRUCT_ARM_THREAD_STATE64      struct __darwin_arm_thread_state64
_STRUCT_ARM_THREAD_STATE64
{
    uint64_t x[29];
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
    uint32_t cpsr;
    uint32_t __pad;
};

typedef _STRUCT_ARM_THREAD_STATE        arm_thread_state_t;
typedef _STRUCT_ARM_THREAD_STATE        arm_thread_state32_t;
typedef _STRUCT_ARM_THREAD_STATE64      arm_thread_state64_t;

#define _STRUCT_ARM_EXCEPTION_STATE64   struct arm_exception_state64
_STRUCT_ARM_EXCEPTION_STATE64
{
    uint64_t far;
    uint32_t esr;
    uint32_t exception;
};

typedef _STRUCT_ARM_EXCEPTION_STATE64   arm_exception_state64_t;

#define _STRUCT_ARM_DEBUG_STATE64       struct __darwin_arm_debug_state64
_STRUCT_ARM_DEBUG_STATE64
{
    uint64_t    bvr[16];
    uint64_t    bcr[16];
    uint64_t    wvr[16];
    uint64_t    wcr[16];
    uint64_t    mdscr_el1;
};

typedef _STRUCT_ARM_DEBUG_STATE64       arm_debug_state64_t;

#define _STRUCT_ARM_VFP_STATE           struct __darwin_arm_vfp_state
_STRUCT_ARM_VFP_STATE
{
    uint32_t    r[64];
    uint32_t    fpscr;
};
typedef _STRUCT_ARM_VFP_STATE           arm_vfp_state_t;

struct arm_vfpv2_state
{
    uint32_t    r[32];
    uint32_t    fpscr;
};
typedef struct arm_vfpv2_state          arm_vfpv2_state_t;

#define _STRUCT_ARM_NEON_STATE64        struct arm_neon_state64
_STRUCT_ARM_NEON_STATE64
{
    __uint128_t q[32];
    uint32_t    fpsr;
    uint32_t    fpcr;
};
typedef _STRUCT_ARM_NEON_STATE64        arm_neon_state64_t;

struct arm_unified_thread_state {
    arm_state_hdr_t ash;
    union {
        arm_thread_state32_t ts_32;
        arm_thread_state64_t ts_64;
    } uts;
};

typedef struct arm_unified_thread_state arm_unified_thread_state_t;

#define ARM_THREAD_STATE_COUNT  ((mach_msg_type_number_t) \
    (sizeof (arm_thread_state_t)/sizeof(uint32_t)))
#define ARM_THREAD_STATE32_COUNT  ((mach_msg_type_number_t) \
    (sizeof (arm_thread_state32_t)/sizeof(uint32_t)))
#define ARM_THREAD_STATE64_COUNT  ((mach_msg_type_number_t) \
    (sizeof (arm_thread_state64_t)/sizeof(uint32_t)))
#define ARM_UNIFIED_THREAD_STATE_COUNT  ((mach_msg_type_number_t) \
    (sizeof (arm_unified_thread_state_t)/sizeof(uint32_t)))


#define ARM_EXCEPTION_STATE64_COUNT ((mach_msg_type_number_t) \
    (sizeof (arm_exception_state64_t)/sizeof(uint32_t)))

#define ARM_DEBUG_STATE64_COUNT ((mach_msg_type_number_t) \
    (sizeof (arm_debug_state64_t)/sizeof(uint32_t)))

#define ARM_VFP_STATE_COUNT ((mach_msg_type_number_t) \
    (sizeof (arm_vfp_state_t)/sizeof(uint32_t)))
#define ARM_VFPV2_STATE_COUNT  ((mach_msg_type_number_t) \
    (sizeof (arm_vfpv2_state_t)/sizeof(uint32_t)))

#define ARM_NEON_STATE64_COUNT  ((mach_msg_type_number_t) \
    (sizeof (arm_neon_state64_t)/sizeof(uint32_t)))

kern_return_t
my_thread_get_state(thread_t thread,
                    int flavor,
                    thread_state_t state,
                    mach_msg_type_number_t *state_count)
{
    uintptr_t retaddr = (uintptr_t)__builtin_return_address(0);
    if(!iqemu_is_code_in_jit(retaddr)) {
        return thread_get_state(thread, flavor, state, state_count);
    }
    
    CPUArchState *env = get_env_by_thread(thread);
    if(NULL == env) {
        qemu_log("thread_get_state: thread not found, falling back to the original routine.\n");
        return thread_get_state(thread, flavor, state, state_count);
    }
    
    switch(flavor) {
    case THREAD_STATE_FLAVOR_LIST:
        if(*state_count < 4)
            return KERN_INVALID_ARGUMENT;
        state[0] = ARM_THREAD_STATE;
        state[1] = ARM_VFP_STATE;
        state[2] = ARM_EXCEPTION_STATE;
        state[3] = ARM_DEBUG_STATE;
        *state_count = 4;
        break;
    case THREAD_STATE_FLAVOR_LIST_NEW:
        if(*state_count < 4)
            return KERN_INVALID_ARGUMENT;
        state[0] = ARM_THREAD_STATE;
        state[1] = ARM_VFP_STATE;
        state[2] = ARM_EXCEPTION_STATE64;
        state[3] = ARM_DEBUG_STATE64;
        *state_count = 4;
        break;
    case ARM_THREAD_STATE: {
        arm_unified_thread_state_t *unified_state = (arm_unified_thread_state_t *)state;
        if(*state_count < ARM_UNIFIED_THREAD_STATE_COUNT)
            return KERN_INVALID_ARGUMENT;
        
        bzero(unified_state, sizeof(*unified_state));
        unified_state->ash.flavor = ARM_THREAD_STATE64;
        unified_state->ash.count = ARM_THREAD_STATE64_COUNT;
        
        memcpy(&unified_state->uts.ts_64.x[0], &env->xregs[0], sizeof(env->xregs));
        unified_state->uts.ts_64.pc = env->pc;
        unified_state->uts.ts_64.cpsr = cpsr_read(env);
        
        *state_count = ARM_UNIFIED_THREAD_STATE_COUNT;
        break;
    }
    case ARM_THREAD_STATE64: {
        arm_thread_state64_t *thread_state = (arm_thread_state64_t *)state;
        if(*state_count < ARM_THREAD_STATE64_COUNT)
            return KERN_INVALID_ARGUMENT;
        memcpy(&thread_state->x[0], &env->xregs[0], sizeof(env->xregs));
        thread_state->pc = env->pc;
        thread_state->cpsr = cpsr_read(env);
        
        *state_count = ARM_THREAD_STATE64_COUNT;
        break;
    }
    case ARM_NEON_STATE64: {
        arm_neon_state64_t *neon_state;
        if(*state_count < ARM_NEON_STATE64_COUNT)
            return KERN_INVALID_ARGUMENT;
        neon_state = (arm_neon_state64_t *)state;
        neon_state->fpsr = vfp_get_fpsr(env);
        neon_state->fpcr = vfp_get_fpcr(env);
        for(int i = 0; i < 32; i ++) {
            neon_state->q[i] = *(__uint128_t *)&env->vfp.zregs[i].d[0];
        }
        *state_count = ARM_NEON_STATE64_COUNT;
    }
    default:
        return KERN_INVALID_ARGUMENT;
    }
    return KERN_SUCCESS;
}
DYLD_INTERPOSE(my_thread_get_state, thread_get_state)

extern int trylock_all_qemu_lock(void);
extern void unlock_all_qemu_lock(void);

kern_return_t
my_thread_suspend(thread_t thread)
{
    uintptr_t retaddr = (uintptr_t)__builtin_return_address(0);
    if (!iqemu_is_code_in_jit(retaddr)) {
        return thread_suspend(thread);
    }
    
    CPUArchState *env = get_env_by_thread(thread);
    if(NULL == env) {
        qemu_log("thread_suspend: thread not found, falling back to the original routine.\n");
        return thread_suspend(thread);
    }
    
    if(GetThreadID_np(thread) == GetThreadID(pthread_self())) {
        //
        // Self does not need a lock here.
        return thread_suspend(thread);
    }
    
    while (0 != trylock_all_qemu_lock()) /* wait */;
    kern_return_t ret = thread_suspend(thread);
    unlock_all_qemu_lock();
    return ret;
}
DYLD_INTERPOSE(my_thread_suspend, thread_suspend)

void* mypthread_get_stackaddr_np(pthread_t thread)
{
    uintptr_t retaddr = (uintptr_t)__builtin_return_address(0);
    if(!iqemu_is_code_in_jit(retaddr)) {
        return pthread_get_stackaddr_np(thread);
    }
    
    CPUArchState *env = get_env_by_pthread(thread);
    if(NULL == env) {
        qemu_log("pthread_get_stackaddr_np: thread not found, falling back to the original routine.\n");
        return pthread_get_stackaddr_np(thread);
    }
    
    return (void *)env->stack_bottom;
}
DYLD_INTERPOSE(mypthread_get_stackaddr_np, pthread_get_stackaddr_np)

kern_return_t myvm_remap(
    vm_map_t target_task,
    vm_address_t *target_address,
    vm_size_t size,
    vm_address_t mask,
    int flags,
    vm_map_t src_task,
    vm_address_t src_address,
    boolean_t copy,
    vm_prot_t *cur_protection,
    vm_prot_t *max_protection,
    vm_inherit_t inheritance
)
{
    kern_return_t r = vm_remap(target_task, target_address, size, mask, flags, src_task, src_address, copy, cur_protection, max_protection, inheritance);
    //while(1) sleep(1);
    if(r == KERN_SUCCESS) {
        // Check for the source address & the target address for the page properties.
        if(need_emulation(src_address)) {
            target_mprotect(*target_address, size, (PROT_READ | PROT_EXEC));
        }
    }
    return r;
}
DYLD_INTERPOSE(myvm_remap, vm_remap)

int
mymprotect(void *addr, size_t size, int prop) {
    /* XXX */
    return mprotect(addr, size, prop);
}
DYLD_INTERPOSE(mymprotect, mprotect)

kern_return_t
myvm_protect(vm_map_t target_task,
             vm_address_t address,
             vm_size_t size,
             boolean_t set_maximum,
             vm_prot_t new_protection)
{
    /* XXX */
    return vm_protect(target_task, address, size, set_maximum, new_protection);
}
DYLD_INTERPOSE(myvm_protect, vm_protect)

kern_return_t
mytask_set_exception_ports(task_t task,
                           exception_mask_t exception_mask,
                           mach_port_t new_port,
                           exception_behavior_t behavior,
                           thread_state_flavor_t new_flavor)
{
    fprintf(stderr, "task_set_exception_ports ignored\n");
    return KERN_SUCCESS;
    //return task_set_exception_ports(task, exception_mask, new_port, behavior, new_flavor);
}
DYLD_INTERPOSE(mytask_set_exception_ports, task_set_exception_ports)

kern_return_t
mythread_set_exception_ports(thread_act_t thread,
                             exception_mask_t exception_mask,
                             mach_port_t new_port,
                             exception_behavior_t behavior,
                             thread_state_flavor_t new_flavor)
{
    fprintf(stderr, "thread_set_exception_ports ignored\n");
    return KERN_SUCCESS;
    //return thread_set_exception_ports(thread, exception_mask, new_port, behavior, new_flavor);
}
DYLD_INTERPOSE(mythread_set_exception_ports, thread_set_exception_ports)

kern_return_t
mytask_swap_exception_ports(task_t task,
                          exception_mask_t exception_mask,
                          mach_port_t new_port,
                          exception_behavior_t behavior,
                          thread_state_flavor_t new_flavor,
                          exception_mask_array_t masks,
                          mach_msg_type_number_t *masksCnt,
                          exception_handler_array_t old_handlerss,
                          exception_behavior_array_t old_behaviors,
                          exception_flavor_array_t old_flavors)
{
    fprintf(stderr, "task_swap_exception_ports ignored!\n");
    return KERN_SUCCESS;
    //return task_swap_exception_ports(task, exception_mask, new_port, behavior, new_flavor, masks, masksCnt, old_handlerss, old_behaviors, old_flavors);
}
DYLD_INTERPOSE(mytask_swap_exception_ports, task_swap_exception_ports)

kern_return_t
mythread_swap_exception_ports(thread_act_t thread,
                              exception_mask_t exception_mask,
                              mach_port_t new_port,
                              exception_behavior_t behavior,
                              thread_state_flavor_t new_flavor,
                              exception_mask_array_t masks,
                              mach_msg_type_number_t *masksCnt,
                              exception_handler_array_t old_handlers,
                              exception_behavior_array_t old_behaviors,
                              exception_flavor_array_t old_flavors)
{
    fprintf(stderr, "thread_swap_exception_ports ignored!\n");
    return KERN_SUCCESS;
    //return thread_swap_exception_ports(thread, exception_mask, new_port, behavior, new_flavor, masks, masksCnt, old_handlers, old_behaviors, old_flavors);
}
DYLD_INTERPOSE(mythread_swap_exception_ports, thread_swap_exception_ports)
/*
extern void NSSetUncaughtExceptionHandler(void *);
void
myNSSetUncaughtExceptionHandler(void * handler)
{
    fprintf(stderr, "NSSetUncaughtExceptionHandler ignored!\n");
    return;
    //myNSSetUncaughtExceptionHandler(handler);
}
DYLD_INTERPOSE(myNSSetUncaughtExceptionHandler, NSSetUncaughtExceptionHandler)
*/
//
// The following 2 functions pose as smoba recursively
// calling functions exported by libsystem_sim_kernel.dylib.
// This happens when smoba tries to fishhook functions from
// libsystem_kernel.dylib, while they are actually re-exported
// by the sim version. Hide sim dylib from smoba to do the trick.

const struct mach_header* my_dyld_get_image_header(uint32_t image_index)
{
    const char *name = _dyld_get_image_name(image_index);
    size_t l = strlen(name);
    const size_t cmpl = strlen("/usr/lib/system/libsystem_sim_kernel.dylib");
    if(name) {
        if(l >= cmpl) {
            if(!strcmp(name + l - cmpl, "/usr/lib/system/libsystem_sim_kernel.dylib")) {
                return _dyld_get_image_header(image_index - 1);
            }
        }
    }
    return _dyld_get_image_header(image_index);
}
DYLD_INTERPOSE(my_dyld_get_image_header, _dyld_get_image_header)

intptr_t my_dyld_get_image_vmaddr_slide(uint32_t image_index)
{
    const char *name = _dyld_get_image_name(image_index);
    size_t l = strlen(name);
    const size_t cmpl = strlen("/usr/lib/system/libsystem_sim_kernel.dylib");
    if(name) {
        if(l >= cmpl) {
            if(!strcmp(name + l - cmpl, "/usr/lib/system/libsystem_sim_kernel.dylib")) {
                return _dyld_get_image_vmaddr_slide(image_index - 1);
            }
        }
    }
    return _dyld_get_image_vmaddr_slide(image_index);
}
DYLD_INTERPOSE(my_dyld_get_image_vmaddr_slide, _dyld_get_image_vmaddr_slide)
