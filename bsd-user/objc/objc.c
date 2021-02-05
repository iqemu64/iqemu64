#include "qemu/osdep.h"
#include "qemu.h"
#include "qemu/x64hooks.h"
#include "exec/exec-all.h"
#include <dlfcn.h>
#include <objc/runtime.h>
#include <objc/message.h>

#define OBJC_X86_MARK           0       // not an actual mark, just an indication.
#define OBJC_ARM_MARK           0x5566DEAD
#define OBJC_ARM_BLOCK_MARK     0xDEAD5566

#define OBJC_GET_IMP_MARK(x)          (*(uint32_t *)((uint8_t *)(x) + 2))
#define OBJC_GET_REAL_IMP(x)          ((*(Method *)((uint8_t *)(x) + 6))->method_imp)

static IMP (* cache_getImp)(Class class, SEL op) = NULL;

extern uintptr_t objc_getMethodNoSuper_nolock;
extern void __MygetMethodNoSuper_nolock(void);

struct Block_info {
    struct Block_layout *block;
    uintptr_t invoke;
};

static void free_block_info(gpointer block);
static pthread_mutex_t gImpBlockLock = PTHREAD_MUTEX_INITIALIZER;
static GHashTable *gImpBlockRecord;

void lock_objc(void)
{
    pthread_mutex_lock(&gImpBlockLock);
}

int trylock_objc(void)
{
    return pthread_mutex_trylock(&gImpBlockLock);
}

void unlock_objc(void)
{
    pthread_mutex_unlock(&gImpBlockLock);
}

static void free_block_info(gpointer block)
{
    g_free(block);
}

IMP myimp_implementationWithBlock(id block)
{
    IMP r = imp_implementationWithBlock(block);
    if(need_emulation(((uintptr_t)((struct Block_layout *)block)->invoke))) {
        id block2 = imp_getBlock(r);    // block is copied.
        
        pthread_mutex_lock(&gImpBlockLock);
        
        struct Block_info *value = g_hash_table_lookup(gImpBlockRecord, r);
        if(!value) {
            value = (struct Block_info *)g_malloc(sizeof(struct Block_info));
            value->block = (struct Block_layout *)block2;
            value->invoke = (uintptr_t)(value->block->invoke);
            
            g_hash_table_insert(gImpBlockRecord, r, value);
        }
        pthread_mutex_unlock(&gImpBlockLock);
    }
    
    return r;
}
DYLD_INTERPOSE(myimp_implementationWithBlock, imp_implementationWithBlock)

BOOL myimp_removeBlock(IMP anImp)
{
    pthread_mutex_lock(&gImpBlockLock);
    g_hash_table_remove(gImpBlockRecord, anImp);
    pthread_mutex_unlock(&gImpBlockLock);
    return imp_removeBlock(anImp);
}
DYLD_INTERPOSE(myimp_removeBlock, imp_removeBlock)

// Query imp Block record
struct Block_info *myimp_getBlock(IMP anImp)
{
    pthread_mutex_lock(&gImpBlockLock);
    struct Block_info *r = (struct Block_info *)g_hash_table_lookup(gImpBlockRecord, anImp);
    pthread_mutex_unlock(&gImpBlockLock);
    return r;
}

static void *objc_msgForward_impcache = NULL;
static inline bool
is_objc_forward(void *imp)
{
    return (imp == objc_msgForward_impcache ||
       imp == _objc_msgForward ||
       imp == _objc_msgForward_stret);
}

void
init_objc_system(const struct MACH_HEADER *header)
{
    int r = 0;
    
    //
    // ImpBlockRecord
    gImpBlockRecord = g_hash_table_new_full(NULL, NULL, NULL, free_block_info);
    
    cache_getImp = (void *)solve_symbol_by_header(
                        header,
                        "_cache_getImp");
    
    if(NULL == cache_getImp) {
        qemu_log("CRITICAL: cache_getImp not found.");
        abort();
    }
    
    // hook.
    objc_getMethodNoSuper_nolock =
        solve_symbol_by_header(header, "__ZL23getMethodNoSuper_nolockP10objc_classP13objc_selector");
    if(0 == objc_getMethodNoSuper_nolock) {
        qemu_log("cannot locate getMethodNoSuper_nolock in libobjc.A.dylib.");
        abort();
    }
    
    r = hook_x64_func(objc_getMethodNoSuper_nolock, (uintptr_t)__MygetMethodNoSuper_nolock);
    if(r < 0) {
        qemu_log("cannot hook getMethodNoSuper_nolock.");
        abort();
    }
    
    objc_msgForward_impcache = (void *)
        solve_symbol_by_header(header, "__objc_msgForward_impcache");
    if(NULL == objc_msgForward_impcache) {
        qemu_log("cannot locate objc_msgForward_impcache.");
        abort();
    }
}

Method
MygetMethodNoSuper_nolock(Class clz, SEL sel, Method result)
{
    if(NULL == result) {
        return result;
    }
    
    uint32_t mark;
    uint8_t *p;
    CPUState *cpu = thread_cpu;
    
    //
    // is result pointing to arm instructions?
    
    if(!need_emulation((uintptr_t)method_getImplementation(result))) {
        //
        // We should know if the imp is a block trampoline?
        // Why not just use imp_getBlock?
        // There is lock inside imp_getBlock that causes deadlock
        // during this lockless call.
        struct Block_info *block = myimp_getBlock(method_getImplementation(result));
        if(NULL == block) {
            //
            // a pure x86 imp, just return.
            return result;
        }
        
        // arm block.
        // need an extra common trampoline to calls from x86 area to avoid a exception.
        // TODO: abort() be replaced.
        //block->block->invoke = (void (*)(void *, ...))
        //    common_query_and_add_trampoline(cpu, (target_ulong)block->invoke, NULL);
        abort();
        mark = OBJC_ARM_BLOCK_MARK;
    } else {
        mark = OBJC_ARM_MARK;
    }
    
    //
    // allocate a trampoline to cpu_xloop so it
    // avoids a huge cost exception.
    
    p = objc_query_and_add_trampoline(cpu, (target_ulong)result,
                                      mark);
    return (Method)(p + 2               // JMP size
                    + sizeof(void *)    // pointer to the real Method
                    + sizeof(uint32_t)  // MARK size.
                    );
}

enum OBJC_MSGSEND_TYPE {
    OBJC_MSGSEND_TYPE_NORMAL,
    OBJC_MSGSEND_TYPE_SUPER,
    OBJC_MSGSEND_TYPE_SUPER2
};

static void
my_objc_msgSend_family(CPUArchState *env, enum OBJC_MSGSEND_TYPE type)
{
    id zelf = NULL;
    SEL selector = NULL;
    Class cls = NULL;
    struct objc_super *super = NULL;
    Method m = NULL;
    IMP imp;

    switch(type) {
    case OBJC_MSGSEND_TYPE_NORMAL:
        zelf = (id)env->xregs[0];
        if(NULL == zelf) goto __plain_ret;
        cls = object_getClass(zelf);
        break;
    case OBJC_MSGSEND_TYPE_SUPER:
        super = (struct objc_super *)env->xregs[0];
        zelf = (id)super->receiver;
        if(NULL == zelf) goto __plain_ret;
        cls = super->class;
        break;
    case OBJC_MSGSEND_TYPE_SUPER2:
        super = (struct objc_super *)env->xregs[0];
        zelf = (id)super->receiver;
        if(NULL == zelf) goto __plain_ret;
        cls = class_getSuperclass(super->class);
        break;
    default:
        g_assert_not_reached();
    }
    
    selector = (SEL)env->xregs[1];
    imp = cache_getImp(cls, selector);
    if(!imp) {
        m = class_getInstanceMethod(cls, selector);
        if(!m) {
            //
            // TODO: It's actually quite legal for missing of the method.
            qemu_log("No method found for %s %s.", class_getName(cls), (const char *)selector);
            abort();
        }
        
        imp = method_getImplementation(m);
    }
    
    //
    // Method MUST have an imp.
    if(!is_objc_forward(imp)) {
        uint32_t mark = OBJC_GET_IMP_MARK(imp);
        if(mark == OBJC_ARM_MARK || mark == OBJC_ARM_BLOCK_MARK) {
            env->pc = OBJC_GET_REAL_IMP(imp);
            return;
        } else {
            //
            // x64 area
            env->pc = (uint64_t)imp;
            if(!m) m = class_getInstanceMethod(cls, selector);
            if(!m) {
                //
                // TODO: It's actually quite legal for missing of the method.
                qemu_log("No method found for %s %s.", class_getName(cls), (const char *)selector);
                abort();
            }
            //
            // Generate the tb in the early stage.
            tb_find_and_gen_by_types(thread_cpu, method_getTypeEncoding(m));
        }
    } else {
        //
        // TODO: dynamic
        abort();
    }
    
    return;
__plain_ret:
    //
    // a plain ret
    env->pc = env->xregs[30];
    env->xregs[0] = env->xregs[1] = 0;
}

void my_objc_msgSend(CPUArchState *env)
{
    my_objc_msgSend_family(env, OBJC_MSGSEND_TYPE_NORMAL);
}

void my_objc_msgSendSuper(CPUArchState *env)
{
    my_objc_msgSend_family(env, OBJC_MSGSEND_TYPE_SUPER);
}

void my_objc_msgSendSuper2(CPUArchState *env)
{
    my_objc_msgSend_family(env, OBJC_MSGSEND_TYPE_SUPER2);
}
