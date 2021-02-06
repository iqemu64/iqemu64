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

/* Code for loading iOS executables.  */

#include "qemu/osdep.h"

#include "qemu.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"

#include <dlfcn.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/vm_types.h>
#include <mach/vm_region.h>
#include <mach/vm_map.h>
#include <mach/mach_init.h>
#include <objc/message.h>
#include "qemu/x64hooks.h"

//
// Local macro defines.


//
// Structure defines.
struct macho_image {
    target_ulong base;
    target_ulong size;
    bool is_foreign;
    const char *path;
};

struct dyld_func {
    const char *name;
    void *implementation;
};      // NOTE this is DIFFERENT than dyld_funcs(with s).

extern void objc_msgSendSuper2(void);
extern void objc_msgSendSuper2_stret(void);

//
// Variable instances.
struct dyld_funcs g_dyld_funcs;
struct objc_msg_funcs g_objc_msg_funcs = {
    (void *)objc_msgSend,
    (void *)objc_msgSendSuper,
    (void *)objc_msgSendSuper2,
    (void *)_objc_msgForward,
};

static struct macho_image g_dyld = { 0, 0, false, "/usr/lib/dyld" };

//
// Function declarations.
static struct MACH_HEADER *mydyld_get_image_header_containing_address(const void *addr);
extern void dyld_stub_binder(void) __asm("dyld_stub_binder");

//
// Function implementations.

bool
is_image_foreign(const struct MACH_HEADER *header)
{
#if defined(TARGET_ARM)
#ifdef TARGET_ABI32
    return (header->cputype == CPU_TYPE_ARM) || (header->flags & MH_EMULATOR);
#else
    return (header->cputype == (CPU_TYPE_ARM | CPU_ARCH_ABI64));
#endif
#else
#error unsupported CPU architecture.
#endif
}

/*
 * we search in the whole symbol table instead of the export ones.
 * we cannot use binary search since the whole table is not sorted.
 * it is kinda slow, think before use it.
 */

uintptr_t
solve_symbol_by_header(const struct MACH_HEADER *header, const char *name)
{
    if(header->magic != MH_MAGIC    && header->magic != MH_CIGAM &&
       header->magic != MH_MAGIC_64 && header->magic != MH_CIGAM_64)
    {
        /* illegal macho */
        return 0;
    }
    uint32_t ncmds = header->ncmds;
    uintptr_t slide = 0;
    uintptr_t fLinkEditBase = 0;
    for(int pass = 1; pass <= 2; pass ++) {
        char *cur_addr = (char *)header + sizeof(struct MACH_HEADER);
        for(uint32_t i = 0; i < ncmds; i ++) {
            struct load_command *lc = (struct load_command *)cur_addr;
            cur_addr += lc->cmdsize;
            switch(lc->cmd) {
                case LC_SEGMENT_COMMAND: {
                    if(pass != 1)
                        break;
                    struct SEGMENT_COMMAND *scp = (struct SEGMENT_COMMAND *)lc;
                    if(!strcmp(scp->segname, "__TEXT")) {
                        slide = (uintptr_t)header - scp->vmaddr;
                    } else if(!strcmp(scp->segname, "__LINKEDIT")) {
                        fLinkEditBase = scp->vmaddr + slide - scp->fileoff;
                    }
                    break;
                }
                case LC_SYMTAB: {
                    if(pass != 2)
                        break;
                    if(fLinkEditBase == 0) {
                        return 0;
                    }
                    struct symtab_command *sm = (struct symtab_command *)lc;
                    char *strtab = (char *)fLinkEditBase + sm->stroff;
                    struct NLIST *symtab = (struct NLIST *)(fLinkEditBase + sm->symoff);
                    const struct NLIST *base = symtab;
                    for(uint32_t n = 0; n < sm->nsyms; n ++) {
                        const struct NLIST *pivot = base + n;
                        const char *pivotStr = strtab + pivot->n_un.n_strx;
                        int cmp = strcmp(name, pivotStr);
                        if(cmp == 0)
                            return pivot->n_value + slide;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        
    }
    
    return 0;
}

const char *
solve_symbol_name(uintptr_t addr)
{
    void *imageLoader = g_dyld_funcs.findMappedRange(addr);
    if(imageLoader) {
        const void *saddr;
        const char *symbol_name = g_dyld_funcs.findClosestSymbol(imageLoader, (const void *)addr, &saddr);
        if((uintptr_t)saddr != addr) {
            // Near is not good enough, it has to be exact.
            return NULL;
        }
        return symbol_name;
    }
    return NULL;
}

const char *
get_name_by_header(const struct MACH_HEADER *header)
{
    Dl_info dlinfo;
    dladdr(header, &dlinfo);
    return dlinfo.dli_fname;
}

const struct MACH_HEADER *
get_mach_header(const void *address)
{
    if((target_ulong)address >= g_dyld.base &&
       (target_ulong)address <  g_dyld.base + g_dyld.size)
    {
        return (const struct MACH_HEADER *)g_dyld.base;
    }
    return mydyld_get_image_header_containing_address(address);
}

extern uintptr_t getLazyBindingInfo;
extern void __MygetLazyBindingInfo(void);

bool
init_dyld_map()
{
    Dl_info dlinfo;
    
    // Here we get the address of libdyld.dylib, not the real dyld_sim!
    if(!dladdr(dladdr, &dlinfo)) {
        return false;
    }
    
    struct MACH_HEADER *mach_header = (struct MACH_HEADER *)dlinfo.dli_fbase;
    uint32_t ncmds = mach_header->ncmds;
    struct load_command *lcp = (struct load_command *)(mach_header + 1);
    uintptr_t slide = 0;
    int found = 0;
    
    while(ncmds --) {
        switch(lcp->cmd) {
        case LC_SEGMENT_COMMAND: {
            struct SEGMENT_COMMAND *scp = (struct SEGMENT_COMMAND *)lcp;
            if(!strcmp(scp->segname, "__TEXT")) {
                slide = (uintptr_t)mach_header - scp->vmaddr;
            } else if(!strcmp(scp->segname, "__DATA")) {
                struct SECTION *sct = (struct SECTION *)(scp + 1);
                struct SECTION *sct_end = (struct SECTION *)((uintptr_t)scp + scp->cmdsize);
                while(sct < sct_end) {
                    if(!strcmp("__dyld", sct->sectname)) {
                        g_dyld_funcs.stub_binding_helper = (void *)*(target_ulong *)(sct->addr + slide);
                        g_dyld_funcs.dyld_func_lookup = (void *)*(target_ulong *)(sct->addr + slide + sizeof(target_ulong));
                        
                        found = 1;
                        break;
                    }
                    sct ++;
                }
            }
            break;
        }
        default:
            break;
        }
        if(found)
            break;
        lcp = (struct load_command *)((uintptr_t)lcp + lcp->cmdsize);
    }

    if(!found)
        return false;
    
    g_dyld_funcs.dyld_stub_binder = (void *)dyld_stub_binder;
    
    kern_return_t krc = KERN_SUCCESS;
    vm_address_t vaddr = (vm_address_t)g_dyld_funcs.dyld_func_lookup;
    vm_size_t size = 0;
    struct vm_region_submap_info_64 info;
    uint32_t depth = 1;
    
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    krc = vm_region_recurse_64(mach_task_self(), &vaddr, &size, &depth, (vm_region_info_64_t)&info, &count);
    if(krc == KERN_INVALID_ADDRESS) {
        /* less likely */
        return false;
    }
    /* so the vaddr now is the mach header of dyld_sim. */
    g_dyld.base = (target_ulong)vaddr;
    g_dyld.size = (target_ulong)size;
    
    const struct dyld_func *dyld_funcs = (const struct dyld_func *)
        solve_symbol_by_header((const struct MACH_HEADER *)vaddr, "__ZL10dyld_funcs");
    if(NULL == dyld_funcs) {
        return false;
    }
    
    while(dyld_funcs->name) {
        /* TODO: add all function protypes into cache system. */
        dyld_funcs ++;
    }
    /* TODO:    add dyld_lookup_func and other __dyld section
     *          functions into cache system. */
    
    g_dyld_funcs.bindLazySymbol =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr, "__ZN4dyld14bindLazySymbolEPK11mach_headerPm");
    if(NULL == g_dyld_funcs.bindLazySymbol)
        abort();
    
    g_dyld_funcs.fastBindLazySymbol =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr, "__ZN4dyld18fastBindLazySymbolEPP11ImageLoaderm");
    if(NULL == g_dyld_funcs.fastBindLazySymbol)
        abort();
    
    g_dyld_funcs.ImageLoaderMachOCompressed_eachBind =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr,
            "__ZN26ImageLoaderMachOCompressed8eachBindERKN11ImageLoader11LinkContextEU13block_pointerFmS3_PS_mhPKchllPN16ImageLoaderMachO13ExtraBindDataES6_PNS_10LastLookupEbE");
    if(NULL == g_dyld_funcs.ImageLoaderMachOCompressed_eachBind)
        abort();
    
    g_dyld_funcs.ImageLoaderMachOCompressed_eachLazyBind =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr,
            "__ZN26ImageLoaderMachOCompressed12eachLazyBindERKN11ImageLoader11LinkContextEU13block_pointerFmS3_PS_mhPKchllPN16ImageLoaderMachO13ExtraBindDataES6_PNS_10LastLookupEbE");
    if(NULL == g_dyld_funcs.ImageLoaderMachOCompressed_eachLazyBind)
        abort();
    
    g_dyld_funcs.findMappedRange =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr,
            "__ZN4dyld15findMappedRangeEm");
    if(NULL == g_dyld_funcs.findMappedRange)
        abort();
    
    g_dyld_funcs.findClosestSymbol =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr,
            "__ZNK26ImageLoaderMachOCompressed17findClosestSymbolEPKvPS1_");
    if(NULL == g_dyld_funcs.findClosestSymbol)
        abort();
    
    g_dyld_funcs.gLinkContext =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr,
            "__ZN4dyld12gLinkContextE");
    if(NULL == g_dyld_funcs.gLinkContext)
        abort();
    
    // hook.
    getLazyBindingInfo =
        solve_symbol_by_header((const struct MACH_HEADER *)vaddr, "__ZN16ImageLoaderMachO18getLazyBindingInfoERjPKhS2_PhPmPiPPKcPb");
    if(0 == getLazyBindingInfo) {
        abort();
    }
    
    int r = hook_x64_func(getLazyBindingInfo, (uintptr_t)__MygetLazyBindingInfo);
    if(r < 0) {
        abort();
    }
    
    return true;
}

struct MACH_HEADER *
mydyld_get_image_header_containing_address(const void *addr)
{
    Dl_info dlinfo;
    if(!dladdr(addr, &dlinfo)) {
        return NULL;
    }
    
    return (struct MACH_HEADER *)dlinfo.dli_fbase;
}

bool get_address_map(const void *address, target_ulong *base, target_ulong *size,
                     target_ulong *lbase, target_ulong *lsize)
{
    struct MACH_HEADER *mach_header = (struct MACH_HEADER *)get_mach_header(address);
    
    *lbase = 0;
    *lsize = 0;
    
    if(mach_header) {
        uint32_t ncmds = mach_header->ncmds;
        if((mach_header->filetype & 0xFF) != MH_EXECUTE) {
            // Not return MH_EXECUTE only.
            return false;
        }
        struct load_command *lcp = (struct load_command *)(mach_header + 1);
        uintptr_t slide = 0;
        
        while(ncmds --) {
            switch(lcp->cmd) {
                case LC_SEGMENT_COMMAND: {
                    struct SEGMENT_COMMAND *scp = (struct SEGMENT_COMMAND *)lcp;
                    if(!strcmp(scp->segname, "__TEXT")) {
                        slide = (uintptr_t)mach_header - scp->vmaddr;
                        *base = (target_ulong)mach_header;
                        *size = (target_ulong)scp->vmsize;
                    } else if(!strcmp(scp->segname, "__LINKEDIT")) {
                        *lbase = (target_ulong)(scp->vmaddr + slide);
                        *lsize = (target_ulong)scp->vmsize;
                    }
                    break;
                }
                default:
                    break;
            }
            
            lcp = (struct load_command *)((uintptr_t)lcp + lcp->cmdsize);
        }
        
        return true;
    }
    
    /* fallback to original method. */
    
    kern_return_t krc = KERN_SUCCESS;
    vm_address_t vaddr = (vm_address_t)address;
    struct vm_region_submap_info_64 info;
    uint32_t depth = 1;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    krc = vm_region_recurse_64(mach_task_self(), &vaddr, size, &depth, (vm_region_info_64_t)&info, &count);
    if(krc == KERN_INVALID_ADDRESS) {
        /* less likely */
        return false;
    }
    
    *base = (target_ulong)vaddr;
    return true;
}

//
// loader exec part

static int
set_segment_exec_protection(const struct load_command *lcp, uintptr_t slide)
{
    struct SEGMENT_COMMAND *scp;
    int flags;
    
    scp = (struct SEGMENT_COMMAND *)lcp;
    
    flags = scp->initprot;
    if(flags & PROT_EXEC) {
        target_mprotect(scp->vmaddr + slide, scp->vmsize, (flags | PROT_EXEC));
    }
    
    return 0;
}

#define INVALID_SLIDE       1       // slide always aligns, 1 is not a good one.
#define ISA_MASK            0x0000000ffffffff8ULL
#define FAST_DATA_MASK      0x00007ffffffffff8UL

struct method_t {
    const char *sel;
    const char *types;
    void *ptr;
};

struct method_list_t {
    uint32_t entsizeAndFlags;
    uint32_t count;
    struct method_t first;
};

struct category_t {
    const char *name;
    void *cls;
    struct method_list_t *instanceMethods;
    struct method_list_t *classMethods;
};

#define RO_REALIZED         (1<<31)

static
void
mark_objc_loads(const struct SEGMENT_COMMAND *scp, uintptr_t slide)
{
    const struct SECTION *sectionsStart = (struct SECTION *)((char *)scp + sizeof(struct SEGMENT_COMMAND));
    const struct SECTION *sectionsEnd = &sectionsStart[scp->nsects];
    for(const struct SECTION *sect = sectionsStart; sect < sectionsEnd; ++sect) {
        if(!memcmp(sect->sectname, "__objc_nlclslist", 16)) {
            uintptr_t *entry = (uintptr_t *)(sect->addr + slide);
            const size_t count = sect->size / sizeof(uintptr_t);
            for(size_t j = 0; j < count; ++j) {
                uintptr_t isa = (*(uintptr_t *)entry[j] & ISA_MASK);
                uintptr_t data = (*(uintptr_t *)(isa + 32)) & FAST_DATA_MASK;
                uint32_t flags = *(uint32_t *)(data);
                uintptr_t ro_data = 0;
                
                if((flags & RO_REALIZED) != 0) {
                    // It is realized, what we get here is class_rw_t
                    // or class_rw_ext_t, depends on the version.
                    ro_data = *(uintptr_t *)(data + 8);
                    if((ro_data & 1) == 1) {
                        ro_data = *(uintptr_t *)(ro_data & ~1ULL);
                    }
                } else {
                    ro_data = data;
                }
                struct method_list_t *baseMethodList = *(struct method_list_t **)(ro_data + 32);
                const uint32_t subcount = baseMethodList->count;
                for(uint32_t i = 0; i < subcount; ++i) {
                    if(!memcmp((&baseMethodList->first)[i].sel, "load", 5)) {
                        tcg_register_callback_as_type((target_ulong)
                                                      (&baseMethodList->first)[i].ptr,
                                                      "v16@0:8");
                        break;
                    }
                }
                
            }
        } else if(!memcmp(sect->sectname, "__objc_nlcatlist", 16)) {
            struct category_t **entry = (struct category_t **)(sect->addr + slide);
            const size_t count = sect->size / sizeof(uintptr_t);
            for(size_t j = 0; j < count; ++j) {
                struct method_list_t *classMethods = entry[j]->classMethods;
                const uint32_t subcount = classMethods->count;
                for(uint32_t i = 0; i < subcount; ++i) {
                    if(!memcmp((&classMethods->first)[i].sel, "load", 5)) {
                        tcg_register_callback_as_type((target_ulong)
                                                      (&classMethods->first)[i].ptr,
                                                      "v16@0:8");
                        break;
                    }
                }
            }
        }
    }
}

static
bool
get_if_lazy_pointers_need_scanning(uintptr_t start, uintptr_t end,
                                   const struct SEGMENT_COMMAND *scp, uintptr_t slide)
{
    assert(slide != INVALID_SLIDE);
    const struct SECTION *sectionsStart = (struct SECTION *)((char *)scp + sizeof(struct SEGMENT_COMMAND));
    const struct SECTION *sectionsEnd = &sectionsStart[scp->nsects];
    for(const struct SECTION *sect = sectionsStart; sect < sectionsEnd; ++sect) {
        const uint8_t type = sect->flags & SECTION_TYPE;
        if(type == S_LAZY_SYMBOL_POINTERS) {
            uintptr_t element = *(uintptr_t *)(sect->addr + slide);
            return !(element >= start && element < end);
        }
    }
    return false;
}

static
void
mark_all_section_calls(uintptr_t mach_header,
                       const struct SEGMENT_COMMAND *scp, uintptr_t slide)
{
    assert(slide != INVALID_SLIDE);
    const struct SECTION *sectionsStart = (struct SECTION *)((char *)scp + sizeof(struct SEGMENT_COMMAND));
    const struct SECTION *sectionsEnd = &sectionsStart[scp->nsects];
    for(const struct SECTION *sect = sectionsStart; sect < sectionsEnd; ++sect) {
        const uint8_t type = sect->flags & SECTION_TYPE;
        if(type == S_MOD_INIT_FUNC_POINTERS) {
            uintptr_t *inits = (uintptr_t *)(sect->addr + slide);
            const size_t count = sect->size / sizeof(uintptr_t);
            for(size_t j = 0; j < count; ++j) {
                uintptr_t func = inits[j];
                tcg_register_callback_as_type(func, "v36i0r^*4r^*12r^*20r^{ProgramVars=^v^i^^*^^*^*}28");
            }
        } else if(type == S_INIT_FUNC_OFFSETS) {
            uint32_t *inits = (uint32_t *)(sect->addr + slide);
            const size_t count = sect->size / sizeof(uintptr_t);
            for(size_t j = 0; j < count; ++j) {
                uintptr_t func = mach_header + inits[j];
                tcg_register_callback_as_type(func, "v36i0r^*4r^*12r^*20r^{ProgramVars=^v^i^^*^^*^*}28");
            }
        } else if(type == S_MOD_TERM_FUNC_POINTERS) {
            uintptr_t *terms = (uintptr_t *)(sect->addr + slide);
            const size_t count = sect->size / sizeof(uintptr_t);
            for(size_t j = 0; j < count; ++j) {
                uintptr_t func = terms[j];
                tcg_register_callback_as_type(func, "v0");
            }
        }
    }
}

static
void
mark_all_bind_symbols(void *imageLoader, bool forceLazy)
{
    mmap_lock();
    g_dyld_funcs.ImageLoaderMachOCompressed_eachBind(imageLoader, g_dyld_funcs.gLinkContext,
            ^(const void *context, void *imageLoaderMachOCompressed_image, uintptr_t addr, uint8_t type, const char *symbolName, uint8_t symbolFlags, intptr_t addend, long libraryOrdinal) {
        uintptr_t target = *(uintptr_t *)addr;
        if(!need_emulation_nolock(target)) {
            g_hash_table_insert(tcg_ctx->addr_to_symbolname,
                                (gpointer)target,
                                (gpointer)symbolName);
        }
        return (uintptr_t)0;
    });
    if(forceLazy) {
        g_dyld_funcs.ImageLoaderMachOCompressed_eachLazyBind(imageLoader, g_dyld_funcs.gLinkContext,
            ^(const void *context, void *imageLoaderMachOCompressed_image, uintptr_t addr, uint8_t type, const char *symbolName, uint8_t symbolFlags, intptr_t addend, long libraryOrdinal) {
            uintptr_t target = *(uintptr_t *)addr;
            if(!need_emulation_nolock(target)) {
                g_hash_table_insert(tcg_ctx->addr_to_symbolname,
                                    (gpointer)target,
                                    (gpointer)symbolName);
            }
            return (uintptr_t)0;
        });
    }
    mmap_unlock();
}

void
MygetLazyBindingInfo(uint32_t *lazyBindingInfoOffset, const uint8_t *lazyInfoStart,
                     const uint8_t *lazyInfoEnd, uint8_t *segIndex,
                     uintptr_t *segOffset, int *ordinal, const char **symbolName,
                     bool *doneAfterBind)
{
    // NOTE: We are inside of the binder, do not call any external
    // functions to avoid infinite recursive
    CPUState *cs = thread_cpu;
    if(cs) {
        // Only responds to threads who owns a env
        CPUArchState *env = cs->env_ptr;
        if(env->arm_binding_lazy_symbol) {
            env->arm_binding_lazy_symbol = false;
            env->lazy_symbol_name = *symbolName;
        }
    }
}

/*
static
void
mark_all_bind_symbols(uintptr_t mach_header,
                       const struct SEGMENT_COMMAND *scp, uintptr_t slide,
                       uint32_t indirectsymoff,
                       const uint8_t *linkedit_base,
                       const struct NLIST *symbol_table,
                       const char *symbol_strings,
                       void *imageLoader)
{
    assert(slide != INVALID_SLIDE);
    const struct SECTION *sectionsStart = (struct SECTION *)((char *)scp + sizeof(struct SEGMENT_COMMAND));
    const struct SECTION *sectionsEnd = &sectionsStart[scp->nsects];
    const uint32_t *const indirectTable = (uint32_t *)(linkedit_base + indirectsymoff);
    
    for(const struct SECTION *sect = sectionsStart; sect < sectionsEnd; ++sect) {
        const uint8_t type = sect->flags & SECTION_TYPE;
        const size_t elementSize = sizeof(uintptr_t);
        
        if(type == S_NON_LAZY_SYMBOL_POINTERS || type == S_LAZY_SYMBOL_POINTERS) {
            size_t elementCount = sect->size / elementSize;
            uintptr_t elements = sect->addr + slide;
            
            mmap_lock();
            for(size_t i = 0; i < elementCount; i++) {
                uint32_t symbolIndex = indirectTable[sect->reserved1 + i];
                if(symbolIndex != INDIRECT_SYMBOL_LOCAL && symbolIndex != INDIRECT_SYMBOL_ABS) {
                    const struct NLIST *sym = &symbol_table[symbolIndex];
                    if(symbolIndex == 0 &&
                       ((const struct MACH_HEADER *)mach_header)->filetype == MH_EXECUTE &&
                       (sym->n_type & N_TYPE) != N_UNDF)
                    {
                        continue;
                    }
                    const char *symbol_string = &symbol_strings[sym->n_un.n_strx];
                    if(type == S_NON_LAZY_SYMBOL_POINTERS) {
                        // Non-lazy part is straight, just bind the address with hint.
                        if(need_emulation_nolock(<#uintptr_t address#>))
                        g_hash_table_insert(tcg_ctx->addr_to_symbolname,
                                            (gpointer)*(uintptr_t *)(elements + i * elementSize),
                                            (gpointer)symbol_string);
                    } else if(type == S_LAZY_SYMBOL_POINTERS) {
                        struct bind_info_hint *bih = g_new0(struct bind_info_hint, 1);
                        bih->symbol_name = symbol_string;
                        bih->addr_to_be_bound = elements + i * elementSize;
                        bih->imageLoader = imageLoader;
                        bih->libraryOrdinal = sym->n_desc;
                        g_hash_table_insert(tcg_ctx->addr_to_symbolname,
                                            (gpointer)*(uintptr_t *)(elements + i * elementSize),
                                            bih);
                    }
                }
            }
            mmap_unlock();
            
        }
    }
}*/

int
loader_exec(const struct MACH_HEADER *header)
{
    int retval = 0;
    struct load_command *lcp;
    const caddr_t addr = (caddr_t)header;
    size_t offset = 0;
    uint32_t ncmds;
    uintptr_t slide = INVALID_SLIDE;
    uintptr_t text_start = 0, text_end = 0;
    bool forceLazy = false;
    const size_t mach_header_sz = sizeof(struct MACH_HEADER);
    
    offset = mach_header_sz;
    ncmds = header->ncmds;
    
    while(ncmds --) {
        lcp = (struct load_command *)(addr + offset);
        offset += lcp->cmdsize;
        
        switch(lcp->cmd) {
            case LC_SEGMENT_COMMAND: {
                struct SEGMENT_COMMAND *scp = (struct SEGMENT_COMMAND *)lcp;
                if(!memcmp(scp->segname, "__TEXT", 7)) {
                    slide = (uintptr_t)header - scp->vmaddr;
                    text_start = (uintptr_t)header;
                    text_end = text_start + scp->vmsize;
                } else if(!memcmp(scp->segname, "__DATA", 7) ||
                          !memcmp(scp->segname, "__DATA_CONST", 13) ||
                          !memcmp(scp->segname, "__DATA_DIRTY", 13)) {
                    assert(slide != INVALID_SLIDE);
                    mark_objc_loads(scp, slide);
                }
                
                if(slide != INVALID_SLIDE) {
                    retval = set_segment_exec_protection(lcp, slide);
                    mark_all_section_calls((uintptr_t)addr, scp, slide);
                    if(!forceLazy)
                        forceLazy = get_if_lazy_pointers_need_scanning(text_start, text_end, scp, slide);
                }
                
                break;
            }
            case LC_MAIN: {
                assert(slide != INVALID_SLIDE);
                target_ulong entry_ptr;
                struct entry_point_command *entry = (struct entry_point_command *)lcp;
                entry_ptr = (target_ulong)(entry->entryoff + (target_ulong)header);
                tcg_register_callback_as_type(entry_ptr, "i16i0[*]8");
                break;
            }
            case LC_ROUTINES_64: {
                assert(slide != INVALID_SLIDE);
                target_ulong entry_ptr;
                struct routines_command_64 *entry = (struct routines_command_64 *)lcp;
                entry_ptr = (target_ulong)(entry->init_address + slide);
                tcg_register_callback_as_type(entry_ptr, "v36i0r^*4r^*12r^*20r^{ProgramVars=^v^i^^*^^*^*}28");
                break;
            }
            
        default:
            break;
        }
        if(retval != 0)
            return(retval);
    }
    
    void *imageLoader = g_dyld_funcs.findMappedRange((uintptr_t)header);
    mark_all_bind_symbols(imageLoader, forceLazy);
    
    return(retval);
}
