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

#ifndef objc_h
#define objc_h

#include <objc/runtime.h>

#define OBJC_X86_MARK           0       // not an actual mark, just an indication.
#define OBJC_ARM_MARK           0x5566DEAD
#define OBJC_ARM_BLOCK_MARK     0xDEAD5566

#define CALLBACK_TRAMPOLINE_MARK 0xDEADBEEF

#define OBJC_GET_IMP_MARK(x)          (*(uint32_t *)((uint8_t *)(x) + 2))
#define OBJC_GET_REAL_IMP(x)          ((*(Method *)((uint8_t *)(x) + 6))->method_imp)

struct Block_layout;
struct iQemu_Block_info {
    struct Block_layout *block;
    uintptr_t orig_invoke;
};

struct iQemu_Block_info *myimp_getBlock(IMP anImp);

static inline
uint32_t
objc_get_real_pc(uintptr_t *inout_pc, void **block)
{
    uint32_t mark = OBJC_GET_IMP_MARK(*inout_pc);
    
    if(mark == OBJC_ARM_MARK) {
        *inout_pc = (uintptr_t)OBJC_GET_REAL_IMP(*inout_pc);
        return mark;
    }else if(mark == OBJC_ARM_BLOCK_MARK) {
        struct iQemu_Block_info *block_info = myimp_getBlock(OBJC_GET_REAL_IMP(*inout_pc));
        *inout_pc = (uintptr_t)block_info->orig_invoke;
        if(block) {
            *block = block_info->block;
        }
        return mark;
    } else if (mark == CALLBACK_TRAMPOLINE_MARK) {
        // see tcg_gen_callback_trampoline()
        const int offset_to_orig_callback = 6;
        
        uint8_t *trampoline = (uint8_t *)(*inout_pc);
        uint64_t callback = *(uint64_t *)(trampoline + offset_to_orig_callback);
        *inout_pc = (uintptr_t)callback;
        return mark;
    }
    
    return OBJC_X86_MARK;
}

#endif /* objc_h */
