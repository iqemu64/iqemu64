/* Code for loading iOS executables.  */

#include "qemu/osdep.h"

#include "qemu.h"
#include "exec/exec-all.h"

#include <dlfcn.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/vm_types.h>
#include <mach/vm_region.h>
#include <mach/vm_map.h>
#include <mach/mach_init.h>
#include <objc/message.h>

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
    (void *)objc_msgSend_stret,
    (void *)objc_msgSendSuper,
    (void *)objc_msgSendSuper_stret,
    (void *)objc_msgSendSuper2,
    (void *)objc_msgSendSuper2_stret
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
solve_symbol_name(uintptr_t addr, const char **libname)
{
    Dl_info info;
    if(libname)
        libname = NULL;
    
    if(dladdr((const void *)addr, &info)) {
        if(addr != (uintptr_t)info.dli_saddr) {
            /* near is not good enough, we need to be exact. */
            return NULL;
        }
        
        if(libname)
            *libname = info.dli_fname;
        return info.dli_sname;
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
    g_dyld_funcs.fastBindLazySymbol =
        (void *)solve_symbol_by_header((const struct MACH_HEADER *)vaddr, "__ZN4dyld18fastBindLazySymbolEPP11ImageLoaderm");
    
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


int
loader_exec(const struct MACH_HEADER *header)
{
    int retval = 0;
    struct load_command *lcp;
    caddr_t addr = (caddr_t)header;
    size_t offset = 0, oldoffset = 0;
    uint32_t ncmds;
    uintptr_t slide = 0;
    
    size_t mach_header_sz = sizeof(struct MACH_HEADER);
    
    offset = mach_header_sz;
    ncmds = header->ncmds;
    
    while(ncmds --) {
        lcp = (struct load_command *)(addr + offset);
        oldoffset = offset;
        offset += lcp->cmdsize;
        
        switch(lcp->cmd) {
            case LC_SEGMENT_COMMAND: {
                struct SEGMENT_COMMAND *scp = (struct SEGMENT_COMMAND *)lcp;
                if(!strcmp(scp->segname, "__TEXT")) {
                    slide = (uintptr_t)header - scp->vmaddr;
                }
                retval = set_segment_exec_protection(lcp, slide);
                break;
            }
            case LC_MAIN: {
                target_ulong entry_ptr;
                struct entry_point_command *entry = (struct entry_point_command *)lcp;
                entry_ptr = (target_ulong)(entry->entryoff + (uintptr_t)header);
                tcg_register_callback_as_type(entry_ptr, "i16i0[*]8");
                break;
            }
        default:
            break;
        }
        if(retval != 0)
            break;
    }
    
    return(retval);
}
