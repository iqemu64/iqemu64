#include "qemu/osdep.h"
#include "mach/vm_param.h"
#include "qemu/x64hooks.h"
#include <sys/mman.h>
#include <mach/vm_region.h>


int
make_exec_page_writable(void *start, size_t len)
{
    uintptr_t start_page = ((uintptr_t)start) & ~PAGE_MASK;
    uintptr_t end_page = ((uintptr_t)start + len) & ~PAGE_MASK;
    uintptr_t progress = 0;
    int r = 0;
    
    while(start_page + progress <= end_page) {
        r = mprotect((void *)(start_page + progress), PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
        if(r != 0) {
            /* we should do a mac-specified trick to enable writing. */
            r = mprotect((void *)(start_page + progress), PAGE_SIZE, VM_PROT_ALL | VM_PROT_COPY);
            if(r != 0) goto __rollback;
        }
        
        progress += PAGE_SIZE;
    }
    return 0;
__rollback:
    progress -= PAGE_SIZE;
    while(start_page + progress >= start_page) {
        (void)mprotect((void *)(start_page + progress), PAGE_SIZE, PROT_READ | PROT_EXEC);
        progress -= PAGE_SIZE;
    }
    return r;
}


int
hook_x64_func(uintptr_t target, uintptr_t stub)
{
    uint8_t buf[] = {
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, // mov rax, #imm64
        0xFF, 0xE0                          // jmp rax
    };
    int size = sizeof(buf);
    
    int r = make_exec_page_writable((void *)target, size);
    if(0 != r) {
        return r;
    }
    
    *(uintptr_t *)(buf + 2) = stub;
    memcpy((void *)target, buf, sizeof(buf));
    
    //
    // Maybe change the page properties back?
    
    return size;
}
