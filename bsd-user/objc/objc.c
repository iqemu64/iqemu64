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
#include "qemu/x64hooks.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include <dlfcn.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <objc/NSObjCRuntime.h>
#include "objc.h"

static IMP (* cache_getImp)(Class class, SEL op) = NULL;

extern uintptr_t objc_getMethodNoSuper_nolock;
extern void __MygetMethodNoSuper_nolock(void);

typedef id (* fn_NSBlock_copy)(Class cls, SEL sel);
id my_NSBlock_copy(Class cls, SEL sel);
fn_NSBlock_copy old_NSBlock_copy = NULL;

typedef id (* fn_NSBlock_copyWithZone)(Class cls, SEL sel,
                                       /* struct _NSZone */ void *zone);
id my_NSBlock_copyWithZone(Class cls, SEL sel, void *zone);
fn_NSBlock_copyWithZone old_NSBlock_copyWithZone = NULL;

static void free_block_info(gpointer block);
static pthread_mutex_t gImpBlockLock = PTHREAD_MUTEX_INITIALIZER;
static GHashTable *gImpBlockRecord;

static SEL forwardingTargetForSelector;
static SEL methodSignatureForSelector;
static SEL getArgumentTypeAtIndex;
static SEL numberOfArguments;
static SEL methodReturnType;
static SEL _frameDescriptor;

static SEL sel_appearanceWhenContainedIn;
static SEL sel_methodForSelector;

typedef void
(*dy_a2x_bridge_trampoline)(uint64_t pc, char *arg_space, unsigned stack_size);

extern bool
get_fn_info_from_types(const char *str, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern void
tcg_register_blocks(struct Block_layout *block);

extern struct Block_layout *
tcg_copy_replace_blocks(struct Block_layout *src);


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
        id block2 = imp_getBlock(r);    // block is copied under imp_implementationWithBlock,
                                        // so no worries about write protections on block.
        
        pthread_mutex_lock(&gImpBlockLock);
        
        struct iQemu_Block_info *value = g_hash_table_lookup(gImpBlockRecord, r);
        if(!value) {
            value = (struct iQemu_Block_info *)g_malloc(sizeof(struct iQemu_Block_info));
            value->block = (struct Block_layout *)block2;
            value->orig_invoke = (uintptr_t)(value->block->invoke);
            
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

void my_method_setImplementation(CPUArchState *env) {
    const uint64_t saved_lr = env->xregs[30];
    
    Method m = (Method)env->xregs[0];
    IMP imp = (IMP)env->xregs[1];
    
    if (need_emulation((uintptr_t)imp)) {
        const char *types = method_getTypeEncoding(m);
        tcg_register_callback_as_type((target_ulong)imp, types);
    }
    
    env->xregs[0] = (uint64_t)method_setImplementation(m, imp);
    
    env->xregs[30] = saved_lr;
    env->pc = env->xregs[30];
}

void my_MTLCreateSystemDefaultDevice(CPUArchState *env) {
    // Metal not supported yet
    env->xregs[0] = 0;
}

// Query imp Block record
struct iQemu_Block_info *myimp_getBlock(IMP anImp)
{
    pthread_mutex_lock(&gImpBlockLock);
    struct iQemu_Block_info *r = (struct iQemu_Block_info *)g_hash_table_lookup(gImpBlockRecord, anImp);
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
    
    forwardingTargetForSelector = sel_getUid("forwardingTargetForSelector:");
    methodSignatureForSelector = sel_getUid("methodSignatureForSelector:");
    getArgumentTypeAtIndex = sel_getUid("getArgumentTypeAtIndex:");
    numberOfArguments = sel_getUid("numberOfArguments");
    methodReturnType = sel_getUid("methodReturnType");
    _frameDescriptor = sel_getUid("_frameDescriptor");
    
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
    
    Method m;
    m = class_getInstanceMethod(objc_getClass("NSBlock"),
                                sel_getUid("copy"));
    old_NSBlock_copy =
        (fn_NSBlock_copy)method_setImplementation(m, (IMP)my_NSBlock_copy);
    
    m = class_getInstanceMethod(objc_getClass("NSBlock"),
                                sel_getUid("copyWithZone:"));
    old_NSBlock_copyWithZone =
        (fn_NSBlock_copyWithZone)method_setImplementation(m, (IMP)my_NSBlock_copyWithZone);
    
    sel_appearanceWhenContainedIn = sel_getUid("appearanceWhenContainedIn:");
    sel_methodForSelector = sel_getUid("methodForSelector:");
}

Method
MygetMethodNoSuper_nolock(Class clz, SEL sel, Method result)
{
    if(NULL == result) {
        return result;
    }
     
    uint8_t *p;
    CPUState *cpu = thread_cpu;
    
    //
    // is result pointing to arm instructions?
    IMP imp = method_getImplementation(result);
    if(!need_emulation((uintptr_t)imp)) {
        //
        // We should know if the imp is a block trampoline?
        // Why not just use imp_getBlock?
        // There is lock inside imp_getBlock that causes deadlock
        // during this lockless call.
        struct iQemu_Block_info *block = myimp_getBlock(imp);
        if(NULL == block) {
            //
            // a pure x86 imp, just return.
            return result;
        }
        
        const char *block_types = block_get_signature(block->block);
        if(NULL == block_types) {
            abort();
        }
        block->block->invoke = (void (*)(void *, ...))
            common_query_and_add_trampoline(cpu, (void *)block->orig_invoke, block_types);
        p = objc_impBlock_query_and_add_trampoline(cpu, result, OBJC_ARM_BLOCK_MARK);
    } else {
        //
        // allocate a trampoline to cpu_xloop so it
        // avoids a huge cost exception.
        
        p = objc_query_and_add_trampoline(cpu, result, OBJC_ARM_MARK);
    }
    return (Method)(p + 2               // JMP size
                    + sizeof(void *)    // pointer to the real Method
                    + sizeof(uint32_t)  // MARK size.
                    );
}

const char *
objc_get_types_from_zelf_selector(CPUArchState *env)
{
    Method m = NULL;
    
    m = class_getInstanceMethod((Class)env->code_gen_hints.cls, (SEL)env->xregs[1]);
    if(NULL == m) {
        //
        // Never should have gone here, objc_msgForward has a specific function handler.
        abort();
    }
    
    return method_getTypeEncoding(m);
}

enum OBJC_MSGSEND_TYPE {
    OBJC_MSGSEND_TYPE_NORMAL,
    OBJC_MSGSEND_TYPE_SUPER,
    OBJC_MSGSEND_TYPE_SUPER2
};

static void
my_objc_msgSend_family(CPUArchState *env, enum OBJC_MSGSEND_TYPE type);

//
// XXX: DANGER: This is an internal structure, and it should
// be changed according to SDK version.
// See ___forwarding___ in CoreFoundation for more information.
// Search for string "method signature and compiler disagree on struct-return-edness of"
#define COMPILE_TIME_ASSERT( expr )    \
    extern int compile_time_assert_failed[ ( expr ) ? 1 : -1 ] __attribute__( ( unused ) );

#define MS_GET_STRET_FLAG(x)    ((x >> 6) & 1)
struct MSFrameDescriptor {
    uint8_t __unused_members[0x22];
    uint16_t flag;
};
COMPILE_TIME_ASSERT(offsetof(struct MSFrameDescriptor, flag) == 0x22);

void
my_objc_msgForward(CPUArchState *env)
{
    /*
     * Once message forwarding has been engaged, we no longer
     * have the privilege to know where it'd go. So we just
     * do our part of job: Translate the parameters as they are,
     * even if we are going back to ARM, the corresponding part of
     * iqemu will do its job, we just don't care in this part.
     * HOWEVER, we still need to take one step further, becuase
     * if the object has a forwarding target, it does not hold
     * the type information we need to do the translation.
     * Yes, the message forwarding has always been a pain in the
     * ass.
     */
    assert(env->code_gen_hints.pc == (uint64_t)_objc_msgForward);
    const uint64_t old_lr = env->xregs[30];
    id zelf = (id)env->xregs[0];
    SEL selector = (SEL)env->xregs[1];
    Class cls = env->code_gen_hints.cls;
    CPUARMParameterRegs saved_state;
    
    save_arm_parameter_regs(env, &saved_state);
    if(class_respondsToSelector(cls, forwardingTargetForSelector)) {
        id target = ((id (*)(id, SEL, SEL))objc_msgSend)(zelf, forwardingTargetForSelector, selector);
        if(target && target != zelf) {
            load_arm_parameter_regs(env, &saved_state);
            env->xregs[0] = (uint64_t)target;
            
            my_objc_msgSend_family(env, OBJC_MSGSEND_TYPE_NORMAL);
            env->xregs[30] = old_lr;
            return;
        }
    }
    
    if(class_respondsToSelector(cls, methodSignatureForSelector)) {
        id ms = ((id (*)(id, SEL, SEL))objc_msgSend)(zelf, methodSignatureForSelector, selector);
        if(!ms) {
            abort();
        }
        
        const char *con = ((const char * (*)(id, SEL))objc_msgSend)(ms, methodReturnType);
        GString *objc_forward_types = g_string_new(con);
        
        NSUInteger numberOfArgs = ((NSUInteger (*)(id, SEL))objc_msgSend)(ms, numberOfArguments);
        for(NSUInteger i = 0; i < numberOfArgs; i ++) {
            con = ((const char * (*)(id, SEL, NSUInteger))objc_msgSend)(ms, getArgumentTypeAtIndex, i);
            if(NULL == con) {
                abort();
            }
            g_string_append(objc_forward_types, con);
        }
        
        load_arm_parameter_regs(env, &saved_state);
        
        char *forward_types = g_string_free(objc_forward_types, false);
        
        struct MSFrameDescriptor **msfd = ((struct MSFrameDescriptor **(*)(id, SEL))objc_msgSend)(ms, _frameDescriptor);
        bool is_stret = MS_GET_STRET_FLAG((**msfd).flag);
        uint64_t real_pc = is_stret ?
                           (uint64_t)_objc_msgForward_stret :
                           (uint64_t)_objc_msgForward;
        {
            ABIFnInfo aaInfo, xxInfo;
            // take it as a non-variadic function
            if (!get_fn_info_from_types(forward_types, &aaInfo, &xxInfo)) {
                qemu_log("parse types failed for %s\n", forward_types);
                abort();
            }
            char *arg_space = (char *)alloca(sizeof(tcg_dy_register_args) +
                                             xxInfo.bytes);
            uint64_t big_enough_sret[8];
            
            abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo,
                                         arg_space, (char *)big_enough_sret);
            
            
            dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
            
            call_x64(real_pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
            
            abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo,
                                        arg_space, (char *)big_enough_sret);
            g_free(aaInfo.ainfo);
            g_free(xxInfo.ainfo);
        }
        
        g_free(forward_types);
    } else {
        qemu_log("class does not respond to message \"methodSignatureForSelector\"\n");
        /* XXX: should probably do a runtime exception process */
        abort();
    }
    env->xregs[30] = old_lr;
    env->pc = env->xregs[30];
}

extern Class _objc_getClassForTag(id tag);

static void
my_objc_msgSend_family(CPUArchState *env, enum OBJC_MSGSEND_TYPE type)
{
    id zelf = NULL;
    SEL selector = NULL;
    Class cls = NULL;
    struct objc_super *super = NULL;
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
        env->xregs[0] = (uint64_t)zelf;       // set the real receiver
        break;
    case OBJC_MSGSEND_TYPE_SUPER2:
        super = (struct objc_super *)env->xregs[0];
        zelf = (id)super->receiver;
        if(NULL == zelf) goto __plain_ret;
        cls = class_getSuperclass(super->class);
        env->xregs[0] = (uint64_t)zelf;       // set the real receiver
        break;
    default:
        g_assert_not_reached();
    }
    
    selector = (SEL)env->xregs[1];
    imp = cache_getImp(cls, selector);
    if(!imp) {
        /*
         * We must use function class_getMethodImplementation,
         * which is the only public function that realize a class
         * that has not been initialized yet.
         * And it's possible that +load gets called during
         * initialization, so it's essential for a stub act-like,
         * namely us, to save the parameter registers.
         */
        
        CPUARMParameterRegs saved_state;
        save_arm_parameter_regs(env, &saved_state);
        imp = class_getMethodImplementation(cls, selector);
        load_arm_parameter_regs(env, &saved_state);
    }
    
    env->pc = (uint64_t)imp;
    /*
     * class_getMethodImplementation always returns objc_msgForward if
     * the method is not found, regardless if it is a struct return.
     */
    if(!is_objc_forward(imp)) {
        uintptr_t inout_pc = (uintptr_t)imp;
        void *block = NULL;
        uint32_t mark = objc_get_real_pc(&inout_pc, &block);
        
        if(mark == OBJC_ARM_MARK) {
            env->pc = (uint64_t)inout_pc;
            return;
        } else if(mark == OBJC_ARM_BLOCK_MARK) {
            env->pc = (uint64_t)inout_pc;
            env->xregs[1] = env->xregs[0];
            env->xregs[0] = (uint64_t)block;
            return;
        } else if (mark == CALLBACK_TRAMPOLINE_MARK) {
            abort();
        }
    } else {
        // objc runtime would fill the cache with objc_msgForward_impcache,
        // set it back.
        env->pc = (uint64_t)(imp = _objc_msgForward);
    }
        
    // x86 area, set the hints in case it is used by tb_gen_code
    env->code_gen_hints.pc = env->pc;
    env->code_gen_hints.code_gen_hint_type = CODE_GEN_HINT_OBJC_CLASS;
    env->code_gen_hints.cls = cls;
    
    env->code_gen_hints.handler = NULL;
    // check for special selectors. Maybe make it a function and cache the handler.
    if (selector == sel_appearanceWhenContainedIn) {
        mmap_lock();
        env->code_gen_hints.handler = tcg_objc_nil_terminated_vari(tcg_ctx, 1);
        env->code_gen_hints.gen_code_size = tcg_current_code_size(tcg_ctx);
        mmap_unlock();
    } else if (selector == sel_methodForSelector) {
        mmap_lock();
        env->code_gen_hints.handler = tcg_target_v_code_gen(tcg_ctx, (tcg_insn_unit *)my_NSObject_methodForSelector);
        env->code_gen_hints.gen_code_size = tcg_current_code_size(tcg_ctx);
        mmap_unlock();
    }
    
    return;
__plain_ret:
    //
    // a plain ret
    env->pc = env->xregs[30];
    env->xregs[0] = env->xregs[1] = 0;
    env->vfp.zregs[0].d[0] = env->vfp.zregs[0].d[1] = 0;
    env->vfp.zregs[1].d[0] = env->vfp.zregs[1].d[1] = 0;
    env->vfp.zregs[2].d[0] = env->vfp.zregs[2].d[1] = 0;
    env->vfp.zregs[3].d[0] = env->vfp.zregs[3].d[1] = 0;
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

id my_NSBlock_copy(Class cls, SEL sel)
{
#if BLOCK_HAS_CALLBACK_TRAMPOLINE
    cls = (Class)tcg_copy_replace_blocks((struct Block_layout *)cls);
#else
    tcg_register_blocks((struct Block_layout *)cls);
#endif
    return old_NSBlock_copy(cls, sel);
}

id my_NSBlock_copyWithZone(Class cls, SEL sel, void *zone)
{
#if BLOCK_HAS_CALLBACK_TRAMPOLINE
    cls = (Class)tcg_copy_replace_blocks((struct Block_layout *)cls);
#else
    tcg_register_blocks((struct Block_layout *)cls);
#endif
    return old_NSBlock_copyWithZone(cls, sel, zone);
}

// objc variadic functions
// these functions are first called by
//      libiqemu_init() -> register_important_funcs()
// data race should not be a problem
#define ins_imp_sel(cls, sel, r)                                    \
do {                                                                \
r = (uintptr_t)class_getMethodImplementation(objc_getClass(cls),    \
                                             sel_getUid(sel));      \
} while (0)

#define cls_imp_sel(cls, sel, r)                                    \
do {                                                                \
r = (uintptr_t)method_getImplementation(                            \
    class_getClassMethod(objc_getClass(cls),                        \
                         sel_getUid(sel)));                         \
} while (0)

uintptr_t
CIFilter_apply(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("CIFilter",
                    "apply:",
                    r);
    return r;
}

uintptr_t
CIFilter_filterWithName_keysAndValues(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("CIFilter",
                    "filterWithName:keysAndValues:",
                    r);
    return r;
}

uintptr_t
CISampler_initWithImage_keysAndValues(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("CISampler",
                    "initWithImage:keysAndValues:",
                    r);
    return r;
}

uintptr_t
CISampler_samplerWithImage_keysAndValues(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("CISampler",
                    "samplerWithImage:keysAndValues:",
                    r);
    return r;
}

uintptr_t
MPSNDArrayDescriptor_descriptorWithDataType_dimensionSizes(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("MPSNDArrayDescriptor",
                    "descriptorWithDataType:dimensionSizes:",
                    r);
    return r;
}

uintptr_t
MPSStateResourceList_resourceListWithBufferSizes(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("MPSStateResourceList",
                    "resourceListWithBufferSizes:",
                    r);
    return r;
}

uintptr_t
MPSStateResourceList_resourceListWithTextureDescriptors(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("MPSStateResourceList",
                    "resourceListWithTextureDescriptors:",
                    r);
    return r;
}

uintptr_t
NSArray_arrayWithObjects(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSArray",
                    "arrayWithObjects:",
                    r);
    return r;
}

uintptr_t
NSArray_initWithObjects(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSArray",
                    "initWithObjects:",
                    r);
    return r;
}

uintptr_t
NSAssertionHandler_handleFailureInFunction_file_lineNumber_description(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSAssertionHandler",
                    "handleFailureInFunction:file:lineNumber:description:",
                    r);
    return r;
}

uintptr_t
NSAssertionHandler_handleFailureInMethod_object_file_lineNumber_description(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSAssertionHandler",
                    "handleFailureInMethod:object:file:lineNumber:description:",
                    r);
    return r;
}

uintptr_t
NSCoder_decodeValuesOfObjCTypes(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSCoder",
                    "decodeValuesOfObjCTypes:",
                    r);
    return r;
}

uintptr_t
NSCoder_encodeValuesOfObjCTypes(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSCoder",
                    "encodeValuesOfObjCTypes:",
                    r);
    return r;
}

uintptr_t
NSDictionary_dictionaryWithObjectsAndKeys(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSDictionary",
                    "dictionaryWithObjectsAndKeys:",
                    r);
    return r;
}

uintptr_t
NSDictionary_initWithObjectsAndKeys(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSDictionary",
                    "initWithObjectsAndKeys:",
                    r);
    return r;
}

uintptr_t __NSDictionaryI_dictionaryWithObjectsAndKeys(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("__NSDictionaryI",
                    "dictionaryWithObjectsAndKeys:",
                    r);
    return r;
}

uintptr_t __NSDictionaryI_initWithObjectsAndKeys(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("__NSDictionaryI",
                    "initWithObjectsAndKeys:",
                    r);
    return r;
}

uintptr_t
NSException_raise_format(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSException",
                    "raise:format:",
                    r);
    return r;
}

uintptr_t
NSExpression_expressionWithFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSExpression",
                    "expressionWithFormat:",
                    r);
    return r;
}

uintptr_t
NSMutableString_appendFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSMutableString",
                    "appendFormat:",
                    r);
    return r;
}

uintptr_t
NSOrderedSet_initWithObjects(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSOrderedSet",
                    "initWithObjects:",
                    r);
    return r;
}

uintptr_t
NSOrderedSet_orderedSetWithObjects(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSOrderedSet",
                    "orderedSetWithObjects:",
                    r);
    return r;
}

uintptr_t
NSPredicate_predicateWithFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSPredicate",
                    "predicateWithFormat:",
                    r);
    return r;
}

uintptr_t
NSSet_initWithObjects(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSSet",
                    "initWithObjects:",
                    r);
    return r;
}

uintptr_t
NSSet_setWithObjects(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSSet",
                    "setWithObjects:",
                    r);
    return r;
}

uintptr_t
NSString_deferredLocalizedIntentsStringWithFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSString",
                    "deferredLocalizedIntentsStringWithFormat:",
                    r);
    return r;
}

uintptr_t
NSString_deferredLocalizedIntentsStringWithFormat_fromTable(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSString",
                    "deferredLocalizedIntentsStringWithFormat:fromTable:",
                    r);
    return r;
}

uintptr_t
NSString_initWithFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSString",
                    "initWithFormat:",
                    r);
    return r;
}

uintptr_t
NSString_initWithFormat_locale(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSString",
                    "initWithFormat:locale:",
                    r);
    return r;
}

uintptr_t
NSString_localizedStringWithFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSString",
                    "localizedStringWithFormat:",
                    r);
    return r;
}

uintptr_t
NSString_stringByAppendingFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSString",
                    "stringByAppendingFormat:",
                    r);
    return r;
}

uintptr_t
NSString_stringWithFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSString",
                    "stringWithFormat:",
                    r);
    return r;
}

uintptr_t
UIActionSheet_initWithTitle_delegate_cancelButtonTitle_destructiveButtonTitle_otherButtonTitles(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("UIActionSheet",
                    "initWithTitle:delegate:cancelButtonTitle:destructiveButtonTitle:otherButtonTitles:",
                    r);
    return r;
}

uintptr_t
UIAlertView_initWithTitle_message_delegate_cancelButtonTitle_otherButtonTitles(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("UIAlertView",
                    "initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:",
                    r);
    return r;
}

uintptr_t
UIAppearance_appearanceForTraitCollection_whenContainedIn(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("UIAppearance",
                    "appearanceForTraitCollection:whenContainedIn:",
                    r);
    return r;
}

uintptr_t
UIAppearance_appearanceWhenContainedIn(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("UIAppearance",
                    "appearanceWhenContainedIn:",
                    r);
    return r;
}

uintptr_t
__NSCFString_appendFormat(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("__NSCFString",
                    "appendFormat:",
                    r);
    return r;
}

uintptr_t
NSException_raise_format_arguments(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSException",
                    "raise:format:arguments:",
                    r);
    return r;
}

uintptr_t
NSExpression_expressionWithFormat_arguments(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSExpression",
                    "expressionWithFormat:arguments:",
                    r);
    return r;
}

uintptr_t
NSPredicate_predicateWithFormat_arguments(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSPredicate",
                    "predicateWithFormat:arguments:",
                    r);
    return r;
}

uintptr_t
NSString_initWithFormat_arguments(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSString",
                    "initWithFormat:arguments:",
                    r);
    return r;
}

uintptr_t
NSString_initWithFormat_locale_arguments(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        ins_imp_sel("NSString",
                    "initWithFormat:locale:arguments:",
                    r);
    return r;
}

uintptr_t
NSString_deferredLocalizedIntentsStringWithFormat_fromTable_arguments(void)
{
    static uintptr_t r = 0;
    if (0 == r)
        cls_imp_sel("NSString",
                    "deferredLocalizedIntentsStringWithFormat:fromTable:arguments:",
                    r);
    return r;
}

#undef cls_imp_sel
#undef ins_imp_sel
