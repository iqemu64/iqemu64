/*
 *  qemu bsd user mode definition
 *
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
#ifndef QEMU_H
#define QEMU_H


#include "cpu.h"
#include "exec/cpu_ldst.h"

#undef DEBUG_REMAP
#ifdef DEBUG_REMAP
#endif /* DEBUG_REMAP */

#include "exec/user/abitypes.h"

enum BSDType {
    target_freebsd,
    target_netbsd,
    target_openbsd,
};
extern enum BSDType bsd_type;

#include "syscall_defs.h"
#include "target_syscall.h"
#include "target_signal.h"
#include "exec/gdbstub.h"

#include <mach/mach.h>

#define THREAD __thread

/* This is the size of the host kernel's sigset_t, needed where we make
 * direct system calls that take a sigset_t pointer and a size.
 */
#define SIGSET_T_SIZE (_NSIG / 8)

/* This struct is used to hold certain information about the image.
 * Basically, it replicates in user space what would be certain
 * task_struct fields in the kernel
 */
struct image_info {
    abi_ulong load_addr;
    abi_ulong start_code;
    abi_ulong end_code;
    abi_ulong start_data;
    abi_ulong end_data;
    abi_ulong arg_start;
    abi_ulong arg_end;
    abi_ulong start_brk;
    abi_ulong brk;
    abi_ulong start_mmap;
    abi_ulong mmap;
    abi_ulong rss;
    abi_ulong start_stack;
    abi_ulong entry;
    abi_ulong code_offset;
    abi_ulong data_offset;
    int       personality;
};

#define MAX_SIGQUEUE_SIZE 1024

struct sigqueue {
    struct sigqueue *next;
    //target_siginfo_t info;
};

struct emulated_sigtable {
    int pending; /* true if signal is pending */
    target_siginfo_t info;
};

/* NOTE: we force a big alignment so that the stack stored after is
   aligned too */
typedef struct TaskState {
    pid_t ts_tid;     /* tid (or pid) of this task */

#ifdef TARGET_ARM
    int swi_errno;
#endif
#if defined(TARGET_ARM) || defined(TARGET_M68K)
    /* Extra fields for semihosted binaries.  */
    abi_ulong heap_base;
    abi_ulong heap_limit;

    abi_ulong stack_base;
#endif
    struct TaskState *next;
    int used; /* non zero if used */
    struct image_info *info;

    struct emulated_sigtable sync_signal;
    struct emulated_sigtable sigtab[TARGET_NSIG];
    /* This thread's signal mask, as requested by the guest program.
     * The actual signal mask of this thread may differ:
     *  + we don't let SIGSEGV and SIGBUS be blocked while running guest code
     *  + sometimes we block all signals to avoid races
     */
    sigset_t signal_mask;
    /* The signal mask imposed by a guest sigsuspend syscall, if we are
     * currently in the middle of such a syscall
     */
    sigset_t sigsuspend_mask;
    /* Nonzero if we're leaving a sigsuspend and sigsuspend_mask is valid. */
    int in_sigsuspend;

    /* Nonzero if process_pending_signals() needs to do something (either
     * handle a pending signal or unblock signals).
     * This flag is written from a signal handler so should be accessed via
     * the atomic_read() and atomic_set() functions. (It is not accessed
     * from multiple threads.)
     */
    int signal_pending;

    /* This thread's sigaltstack, if it has one */
    struct target_sigaltstack sigaltstack_used;
} __attribute__((aligned(16))) TaskState;

void init_task_state(TaskState *ts);
extern const char *qemu_uname_release;
extern unsigned long mmap_min_addr;


void do_init_thread(struct target_pt_regs *regs, struct image_info *infop);
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr);

abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len);
void target_set_brk(abi_ulong new_brk);
abi_long do_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8);
abi_long do_netbsd_syscall(void *cpu_env, int num, abi_long arg1,
                           abi_long arg2, abi_long arg3, abi_long arg4,
                           abi_long arg5, abi_long arg6);
abi_long do_openbsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6);
void gemu_log(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
extern __thread CPUState *thread_cpu;
void cpu_loop(CPUArchState *env);
char *target_strerror(int err);
int get_osversion(void);
void fork_start(void);
void fork_end(int child);

#include "qemu/log.h"

/* strace.c */
struct syscallname {
    int nr;
    const char *name;
    const char *format;
    void (*call)(const struct syscallname *,
                 abi_long, abi_long, abi_long,
                 abi_long, abi_long, abi_long);
    void (*result)(const struct syscallname *, abi_long);
};

void
print_freebsd_syscall(int num,
                      abi_long arg1, abi_long arg2, abi_long arg3,
                      abi_long arg4, abi_long arg5, abi_long arg6);
void print_freebsd_syscall_ret(int num, abi_long ret);
void
print_netbsd_syscall(int num,
                     abi_long arg1, abi_long arg2, abi_long arg3,
                     abi_long arg4, abi_long arg5, abi_long arg6);
void print_netbsd_syscall_ret(int num, abi_long ret);
void
print_openbsd_syscall(int num,
                      abi_long arg1, abi_long arg2, abi_long arg3,
                      abi_long arg4, abi_long arg5, abi_long arg6);
void print_openbsd_syscall_ret(int num, abi_long ret);
extern int do_strace;

/* signal.c */
void process_pending_signals(CPUArchState *cpu_env);
void signal_init(void);
//int queue_signal(CPUArchState *env, int sig, target_siginfo_t *info);
//void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info);
//void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo);
int host_to_target_signal(int sig);
int queue_signal(CPUArchState *env, int sig, int si_type,
                 target_siginfo_t *info);
void target_to_host_sigset(sigset_t *d, const target_sigset_t *s);
void host_to_target_sigset(target_sigset_t *d, const sigset_t *s);
void target_to_host_sigaction(struct sigaction *d, const struct target_sigaction *s);
void host_to_target_sigaction(struct target_sigaction *d, const struct sigaction *s);
int do_sigaction(int sig, const struct target_sigaction *act, struct target_sigaction *oact);
long do_sigreturn(CPUArchState *env);
long do_rt_sigreturn(CPUArchState *env);
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr, abi_ulong sp);

/* mmap.c */
int target_mprotect(abi_ulong start, abi_ulong len, int prot);
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, abi_ulong offset);
int target_munmap(abi_ulong start, abi_ulong len);
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr);
int target_msync(abi_ulong start, abi_ulong len, int flags);
extern unsigned long last_brk;
void mmap_fork_start(void);
void mmap_fork_end(int child);

/* main.c */
extern unsigned long x86_stack_size;

CPUArchState *create_arch_cpu(void);

void CFIList_Truncate_To_By_Count(CPUArchState *env, int count);
void CFIList_Truncate_To(CPUArchState *env, CFIEntry *entry);

/* osxutils.c */

uint64_t GetThreadID(pthread_t thread);
uint64_t GetThreadID_np(mach_port_name_t thread);
CPUArchState *get_env_by_thread(thread_t thread);
CPUArchState *get_env_by_pthread(pthread_t thread);

/* user access */

#define VERIFY_READ 0
#define VERIFY_WRITE 1 /* implies read access */

static inline int access_ok(int type, abi_ulong addr, abi_ulong size)
{
    return page_check_range((target_ulong)addr, size,
                            (type == VERIFY_READ) ? PAGE_READ : (PAGE_READ | PAGE_WRITE)) == 0;
}

/* NOTE __get_user and __put_user use host pointers and don't check access. */
/* These are usually used to access struct data members once the
 * struct has been locked - usually with lock_user_struct().
 */
#define __put_user(x, hptr)\
({\
    int size = sizeof(*hptr);\
    switch(size) {\
    case 1:\
        *(uint8_t *)(hptr) = (uint8_t)(typeof(*hptr))(x);\
        break;\
    case 2:\
        *(uint16_t *)(hptr) = tswap16((typeof(*hptr))(x));\
        break;\
    case 4:\
        *(uint32_t *)(hptr) = tswap32((typeof(*hptr))(x));\
        break;\
    case 8:\
        *(uint64_t *)(hptr) = tswap64((typeof(*hptr))(x));\
        break;\
    default:\
        abort();\
    }\
    0;\
})

#define __get_user(x, hptr) \
({\
    int size = sizeof(*hptr);\
    switch(size) {\
    case 1:\
        x = (typeof(*hptr))*(uint8_t *)(hptr);\
        break;\
    case 2:\
        x = (typeof(*hptr))tswap16(*(uint16_t *)(hptr));\
        break;\
    case 4:\
        x = (typeof(*hptr))tswap32(*(uint32_t *)(hptr));\
        break;\
    case 8:\
        x = (typeof(*hptr))tswap64(*(uint64_t *)(hptr));\
        break;\
    default:\
        /* avoid warning */\
        x = 0;\
        abort();\
    }\
    0;\
})

/* put_user()/get_user() take a guest address and check access */
/* These are usually used to access an atomic data type, such as an int,
 * that has been passed by address.  These internally perform locking
 * and unlocking on the data type.
 */
#define put_user(x, gaddr, target_type)                                 \
({                                                                      \
    abi_ulong __gaddr = (gaddr);                                        \
    target_type *__hptr;                                                \
    abi_long __ret;                                                     \
    if ((__hptr = lock_user(VERIFY_WRITE, __gaddr, sizeof(target_type), 0))) { \
        __ret = __put_user((x), __hptr);                                \
        unlock_user(__hptr, __gaddr, sizeof(target_type));              \
    } else                                                              \
        __ret = -TARGET_EFAULT;                                         \
    __ret;                                                              \
})

#define get_user(x, gaddr, target_type)                                 \
({                                                                      \
    abi_ulong __gaddr = (gaddr);                                        \
    target_type *__hptr;                                                \
    abi_long __ret;                                                     \
    if ((__hptr = lock_user(VERIFY_READ, __gaddr, sizeof(target_type), 1))) { \
        __ret = __get_user((x), __hptr);                                \
        unlock_user(__hptr, __gaddr, 0);                                \
    } else {                                                            \
        /* avoid warning */                                             \
        (x) = 0;                                                        \
        __ret = -TARGET_EFAULT;                                         \
    }                                                                   \
    __ret;                                                              \
})

#define put_user_ual(x, gaddr) put_user((x), (gaddr), abi_ulong)
#define put_user_sal(x, gaddr) put_user((x), (gaddr), abi_long)
#define put_user_u64(x, gaddr) put_user((x), (gaddr), uint64_t)
#define put_user_s64(x, gaddr) put_user((x), (gaddr), int64_t)
#define put_user_u32(x, gaddr) put_user((x), (gaddr), uint32_t)
#define put_user_s32(x, gaddr) put_user((x), (gaddr), int32_t)
#define put_user_u16(x, gaddr) put_user((x), (gaddr), uint16_t)
#define put_user_s16(x, gaddr) put_user((x), (gaddr), int16_t)
#define put_user_u8(x, gaddr)  put_user((x), (gaddr), uint8_t)
#define put_user_s8(x, gaddr)  put_user((x), (gaddr), int8_t)

#define get_user_ual(x, gaddr) get_user((x), (gaddr), abi_ulong)
#define get_user_sal(x, gaddr) get_user((x), (gaddr), abi_long)
#define get_user_u64(x, gaddr) get_user((x), (gaddr), uint64_t)
#define get_user_s64(x, gaddr) get_user((x), (gaddr), int64_t)
#define get_user_u32(x, gaddr) get_user((x), (gaddr), uint32_t)
#define get_user_s32(x, gaddr) get_user((x), (gaddr), int32_t)
#define get_user_u16(x, gaddr) get_user((x), (gaddr), uint16_t)
#define get_user_s16(x, gaddr) get_user((x), (gaddr), int16_t)
#define get_user_u8(x, gaddr)  get_user((x), (gaddr), uint8_t)
#define get_user_s8(x, gaddr)  get_user((x), (gaddr), int8_t)

/* NOTE __get_user and __put_user use host pointers and don't check access.
   These are usually used to access struct data members once the struct has
   been locked - usually with lock_user_struct.  */

/*
 * Tricky points:
 * - Use __builtin_choose_expr to avoid type promotion from ?:,
 * - Invalid sizes result in a compile time error stemming from
 *   the fact that abort has no parameters.
 * - It's easier to use the endian-specific unaligned load/store
 *   functions than host-endian unaligned load/store plus tswapN.
 * - The pragmas are necessary only to silence a clang false-positive
 *   warning: see https://bugs.llvm.org/show_bug.cgi?id=39113 .
 * - gcc has bugs in its _Pragma() support in some versions, eg
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=83256 -- so we only
 *   include the warning-suppression pragmas for clang
 */
#if defined(__clang__) && __has_warning("-Waddress-of-packed-member")
#define PRAGMA_DISABLE_PACKED_WARNING                                   \
    _Pragma("GCC diagnostic push");                                     \
    _Pragma("GCC diagnostic ignored \"-Waddress-of-packed-member\"")

#define PRAGMA_REENABLE_PACKED_WARNING          \
    _Pragma("GCC diagnostic pop")

#else
#define PRAGMA_DISABLE_PACKED_WARNING
#define PRAGMA_REENABLE_PACKED_WARNING
#endif

#define __put_user_e(x, hptr, e)                                            \
    do {                                                                    \
        PRAGMA_DISABLE_PACKED_WARNING;                                      \
        (__builtin_choose_expr(sizeof(*(hptr)) == 1, stb_p,                 \
        __builtin_choose_expr(sizeof(*(hptr)) == 2, stw_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 4, stl_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 8, stq_##e##_p, abort))))  \
            ((hptr), (x)), (void)0);                                        \
        PRAGMA_REENABLE_PACKED_WARNING;                                     \
    } while (0)

#define __get_user_e(x, hptr, e)                                            \
    do {                                                                    \
        PRAGMA_DISABLE_PACKED_WARNING;                                      \
        ((x) = (typeof(*hptr))(                                             \
        __builtin_choose_expr(sizeof(*(hptr)) == 1, ldub_p,                 \
        __builtin_choose_expr(sizeof(*(hptr)) == 2, lduw_##e##_p,           \
        __builtin_choose_expr(sizeof(*(hptr)) == 4, ldl_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 8, ldq_##e##_p, abort))))  \
            (hptr)), (void)0);                                              \
        PRAGMA_REENABLE_PACKED_WARNING;                                     \
    } while (0)

#define DYLD_INTERPOSE(_replacement,_replacee) \
    __attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
    __attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };

/* copy_from_user() and copy_to_user() are usually used to copy data
 * buffers between the target and host.  These internally perform
 * locking/unlocking of the memory.
 */
abi_long copy_from_user(void *hptr, abi_ulong gaddr, size_t len);
abi_long copy_to_user(abi_ulong gaddr, void *hptr, size_t len);

/* Functions for accessing guest memory.  The tget and tput functions
   read/write single values, byteswapping as necessary.  The lock_user function
   gets a pointer to a contiguous area of guest memory, but does not perform
   any byteswapping.  lock_user may return either a pointer to the guest
   memory, or a temporary buffer.  */

/* Lock an area of guest memory into the host.  If copy is true then the
   host area will have the same contents as the guest.  */
static inline void *lock_user(int type, abi_ulong guest_addr, long len, int copy)
{
    if (!access_ok(type, guest_addr, len))
        return NULL;
#ifdef DEBUG_REMAP
    {
        void *addr;
        addr = g_malloc(len);
        if (copy)
            memcpy(addr, g2h(guest_addr), len);
        else
            memset(addr, 0, len);
        return addr;
    }
#else
    return g2h(guest_addr);
#endif
}

/* Unlock an area of guest memory.  The first LEN bytes must be
   flushed back to guest memory. host_ptr = NULL is explicitly
   allowed and does nothing. */
static inline void unlock_user(void *host_ptr, abi_ulong guest_addr,
                               long len)
{

#ifdef DEBUG_REMAP
    if (!host_ptr)
        return;
    if (host_ptr == g2h(guest_addr))
        return;
    if (len > 0)
        memcpy(g2h(guest_addr), host_ptr, len);
    g_free(host_ptr);
#endif
}

#define EXPORT __attribute__((visibility("default")))

bool need_emulation(uintptr_t address);
static inline bool need_emulation_nolock(uintptr_t address)
{
    return page_get_flags(address) & PAGE_EXEC_ORG ? true : false;
}

/* Return the length of a string in target memory or -TARGET_EFAULT if
   access error. */
abi_long target_strlen(abi_ulong gaddr);

/* Like lock_user but for null terminated strings.  */
static inline void *lock_user_string(abi_ulong guest_addr)
{
    abi_long len;
    len = target_strlen(guest_addr);
    if (len < 0)
        return NULL;
    return lock_user(VERIFY_READ, guest_addr, (long)(len + 1), 1);
}

/* Helper macros for locking/unlocking a target struct.  */
#define lock_user_struct(type, host_ptr, guest_addr, copy)      \
    (host_ptr = lock_user(type, guest_addr, sizeof(*host_ptr), copy))
#define unlock_user_struct(host_ptr, guest_addr, copy)          \
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

#if defined(CONFIG_USE_NPTL)
#include <pthread.h>
#endif

// hooks.c

void hook_self_malloc(void);

// log.m

int RedirectOutputToView(void *inFD, const char *buffer, int size);

// iosload.c

#ifdef TARGET_ABI32
#define LC_SEGMENT_COMMAND      LC_SEGMENT
#define SEGMENT_COMMAND         segment_command
#define SECTION                 section
#define MACH_HEADER             mach_header
#define NLIST                   nlist
#else
#define LC_SEGMENT_COMMAND      LC_SEGMENT_64
#define SEGMENT_COMMAND         segment_command_64
#define SECTION                 section_64
#define MACH_HEADER             mach_header_64
#define NLIST                   nlist_64
#endif

typedef uintptr_t       (^bind_handler)(const void *context, void *imageLoaderMachOCompressed_image, uintptr_t addr, uint8_t type, const char *symbolName, uint8_t symbolFlags, intptr_t addend, long libraryOrdinal);
// There are actucally more parameters for this block. but we tend to ignore that, will be alright.

struct MACH_HEADER;
struct dyld_funcs {
    // Exported(?) functions.
    void *stub_binding_helper;
    void *dyld_stub_binder;
    bool (*dyld_func_lookup)(const char *name, void **address);
    
    // Internal functions.
    uintptr_t ( *bindLazySymbol)(const struct MACH_HEADER *mh, uintptr_t *lazyPointer);
    uintptr_t ( *fastBindLazySymbol)(void *imageLoaderCache, uintptr_t lazyBindingInfoOffset);
    
    
    void ( *ImageLoaderMachOCompressed_eachBind)(void *imageLoaderMachOCompressed_this, const void *link_context, bind_handler handler);
    void ( *ImageLoaderMachOCompressed_eachLazyBind)(void *imageLoaderMachOCompressed_this, const void *link_context, bind_handler handler);
    void *( *findMappedRange)(uintptr_t target);
    const char *( *findClosestSymbol)(void *pthis, const void *addr, const void **closestAddr);
    void *gLinkContext;
};

struct objc_msg_funcs {
    void *objc_msgSend;
    void *objc_msgSendSuper;
    void *objc_msgSendSuper2;
    void *objc_msgForward;
};

extern struct dyld_funcs g_dyld_funcs;
extern struct objc_msg_funcs g_objc_msg_funcs;

bool init_dyld_map(void);

bool is_image_foreign(const struct MACH_HEADER *header);
const struct MACH_HEADER *get_mach_header(const void *address);
const char *get_name_by_header(const struct MACH_HEADER *header);
const char *solve_symbol_name(uintptr_t addr);
uintptr_t solve_symbol_by_header(const struct MACH_HEADER *header, const char *name);

#include "dbg.h"

bool get_address_map(const void *address, target_ulong *base, target_ulong *size,
                     target_ulong *lbase, target_ulong *lsize);

int loader_exec(const struct MACH_HEADER *header);

//
// appcomp.m

void app_compatibility_level(const struct MACH_HEADER *header, intptr_t slide);
const char *smoba_class_addMethod(const char *selector);
//
// objc.m

void init_objc_system(const struct MACH_HEADER *header);

//
// gcd.c

#define BLOCK_HAS_CALLBACK_TRAMPOLINE 1
#define BLOCK_BYREF_NEEDS_COPY 0
#define BLOCK_NEEDS_STACK_COPY 0
/*
 * In order to replace original block invoke/copy/dispose(s) with our callback
 * trampolines, the constant part (e.g., Global_block, all block descriptors)
 * is copied to heap, and never freed.
 * Lucky that there is no such thing as Global Block_byref.
 */

enum {
    BLOCK_DEALLOCATING =        (0x0001),
    BLOCK_REFCOUNT_MASK =       (0xfffe),
    BLOCK_NEEDS_FREE =          (1 << 24),
    BLOCK_HAS_COPY_DISPOSE =    (1 << 25),
    BLOCK_HAS_CTOR =            (1 << 26),
    BLOCK_IS_GC =               (1 << 27),
    BLOCK_IS_GLOBAL =           (1 << 28),
    BLOCK_USE_STRET =           (1 << 29),
    BLOCK_HAS_SIGNATURE =       (1 << 30),
    BLOCK_HAS_EXTENDED_LAYOUT = (1 << 31)
};

struct Block_descriptor_1 {
    uintptr_t reserved;
    uintptr_t size;
};

struct Block_descriptor_2 {
    void (*copy)(void *dst, const void *src);
    void (*dispose)(const void *);
};

struct Block_descriptor_3 {
    const char *signature;
    const char *layout;
};


struct Block_layout {
    void *isa;
    volatile int32_t flags;
    int32_t reserved;
    void (* invoke)(void *, ...);
    struct Block_descriptor_1 *descriptor;
};

// Values for Block_byref->flags to describe __block variables
enum {
    // Byref refcount must use the same bits as Block_layout's refcount.
    // BLOCK_DEALLOCATING =      (0x0001),  // runtime
    // BLOCK_REFCOUNT_MASK =     (0xfffe),  // runtime

    BLOCK_BYREF_LAYOUT_MASK =       (0xf << 28), // compiler
    BLOCK_BYREF_LAYOUT_EXTENDED =   (  1 << 28), // compiler
    BLOCK_BYREF_LAYOUT_NON_OBJECT = (  2 << 28), // compiler
    BLOCK_BYREF_LAYOUT_STRONG =     (  3 << 28), // compiler
    BLOCK_BYREF_LAYOUT_WEAK =       (  4 << 28), // compiler
    BLOCK_BYREF_LAYOUT_UNRETAINED = (  5 << 28), // compiler

    BLOCK_BYREF_IS_GC =             (  1 << 27), // runtime

    BLOCK_BYREF_HAS_COPY_DISPOSE =  (  1 << 25), // compiler
    BLOCK_BYREF_NEEDS_FREE =        (  1 << 24), // runtime
};

// Values for _Block_object_assign() and _Block_object_dispose() parameters
enum {
    // see function implementation for a more complete description of these fields and combinations
    BLOCK_FIELD_IS_OBJECT   =  3,  // id, NSObject, __attribute__((NSObject)), block, ...
    BLOCK_FIELD_IS_BLOCK    =  7,  // a block variable
    BLOCK_FIELD_IS_BYREF    =  8,  // the on stack structure holding the __block variable
    BLOCK_FIELD_IS_WEAK     = 16,  // declared __weak, only used in byref copy helpers
    BLOCK_BYREF_CALLER      = 128, // called from __block (byref) copy/dispose support routines.
};

enum {
    BLOCK_ALL_COPY_DISPOSE_FLAGS =
        BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_BLOCK | BLOCK_FIELD_IS_BYREF |
        BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER
};

struct Block_byref {
    void *isa;
    struct Block_byref *forwarding;
    volatile int32_t flags; // contains ref count
    uint32_t size;
};

typedef void(*BlockCopyFunction)(void *, const void *);
typedef void(*BlockDisposeFunction)(const void *);
typedef void(*BlockInvokeFunction)(void *, ...);
typedef void(*BlockByrefKeepFunction)(struct Block_byref*, struct Block_byref*);
typedef void(*BlockByrefDestroyFunction)(struct Block_byref *);
struct Block_byref_2 {
    // requires BLOCK_BYREF_HAS_COPY_DISPOSE
    BlockByrefKeepFunction byref_keep;
    BlockByrefDestroyFunction byref_destroy;
};

struct Block_byref_3 {
    // requires BLOCK_BYREF_LAYOUT_EXTENDED
    const char *layout;
};

static inline BlockInvokeFunction
block_get_invoke(struct Block_layout *block)
{
    return block->invoke;
}

static inline void
block_set_invoke(struct Block_layout *block, uint8_t *invoke)
{
    block->invoke = (BlockInvokeFunction)invoke;
}

// align v up to a multiple of a; a should be power of 2
#define ALIGN2POW(v, a) ((((v)-1)|((a)-1))+1)

static inline size_t
block_get_size_for_heap_copy(struct Block_layout *block)
{
    size_t s = block->descriptor->size;
    return s;
}

// descriptors are not included in the block size
static inline size_t
block_get_aligned_size_for_stack_copy(struct Block_layout *block)
{
    size_t overhead = sizeof(struct Block_layout *) /* for original block */ +
        sizeof(struct Block_layout *) /* for copied block */;
    
    if (NULL == block)
        return ALIGN2POW(overhead, 16);
        
    size_t s = overhead + block->descriptor->size;
    return ALIGN2POW(s, 16);
}

struct Block_layout *block_copy_to_stack_or_heap(uint8_t *u8dst, struct Block_layout *src);
struct Block_layout *block_copy_to_heap_or_not(struct Block_layout *src);
struct Block_layout *block_update_from_stack(uint8_t *stack);
static inline void
block_update_from_copied(struct Block_layout *copied, struct Block_layout *orig)
{
    if (orig->flags != copied->flags)
        orig->flags = copied->flags;
}

const char *block_get_signature(struct Block_layout *block);
void block_get_copy_dispose(struct Block_layout *block, void (**copy)(void *dst, const void *src),
                       void (**dispose)(const void *));
void block_set_copy_dispose(struct Block_layout *block, uint8_t *copy, uint8_t *dispose);

static inline size_t
block_byref_get_size_for_heap_copy(struct Block_byref *bb)
{
    return bb->size;
}

static inline size_t
block_byref_get_aligned_size_for_stack_copy(struct Block_byref *bb)
{
    size_t overhead = sizeof(struct Block_byref *) * 2;
    
    if (NULL == bb)
        return ALIGN2POW(overhead, 16);
    
    size_t s = overhead + bb->size;
    return ALIGN2POW(s, 16);
}

struct Block_byref *block_byref_copy_to_stack_or_heap(uint8_t *u8dst, struct Block_byref *src);
struct Block_byref *block_byref_copy_to_heap_or_not(struct Block_byref *src);
struct Block_byref *block_byref_update_from_stack(uint8_t *stack);
static inline void
block_byref_update_from_copied(struct Block_byref *copied, struct Block_byref *orig)
{
    if (orig->flags != copied->flags)
        orig->flags = copied->flags;
    if (orig->forwarding != copied->forwarding)
        orig->forwarding = copied->forwarding;
}

void block_byref_get_keep_destroy(struct Block_byref *bb,
                                  BlockByrefKeepFunction *keep,
                                  BlockByrefDestroyFunction *destroy);
void block_byref_set_keep_destroy(struct Block_byref *bb,
                                  uint8_t *keep,
                                  uint8_t *destroy);

static inline struct Block_byref *
block_byref_get_forwarding(struct Block_byref *bb)
{
    return bb->forwarding;
}

void init_block_system(const struct MACH_HEADER *header);

//
// osxutils.c

//
// syscall.c
int host_to_target_waitstatus(int status);
abi_long do_syscall(void *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8);

// signal.c
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

#endif /* QEMU_H */
