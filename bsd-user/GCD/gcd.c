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
#include <Block.h>

static struct Block_descriptor_2 *
block_get_descriptor2(struct Block_layout *block)
{
    if(!(block->flags & BLOCK_HAS_COPY_DISPOSE)) return NULL;
    uint8_t *desc = (uint8_t *)block->descriptor;
    desc += sizeof(struct Block_descriptor_1);
    return (struct Block_descriptor_2 *)desc;
}

static struct Block_descriptor_3 *
block_get_descriptor3(struct Block_layout *block)
{
    if(!(block->flags & BLOCK_HAS_SIGNATURE)) return NULL;
    uint8_t *desc = (uint8_t *)block->descriptor;
    desc += sizeof(struct Block_descriptor_1);
    if(block->flags & BLOCK_HAS_COPY_DISPOSE) {
        desc += sizeof(struct Block_descriptor_2);
    }
    return (struct Block_descriptor_3 *)desc;
}

const char *
block_get_signature(struct Block_layout *block)
{
    struct Block_descriptor_3 *bd3 = block_get_descriptor3(block);
    if(!bd3) return NULL;
    return bd3->signature;
}

void
block_get_copy_dispose(struct Block_layout *block, void (**copy)(void *dst, const void *src),
                       void (**dispose)(const void *))
{
    struct Block_descriptor_2 *bd2 = block_get_descriptor2(block);
    if(!bd2) {
        *copy = NULL;
        *dispose = NULL;
        return;
    }
    
    *copy = bd2->copy;
    *dispose = bd2->dispose;
}

static void
my_block_copy(struct Block_layout *dst, struct Block_layout *src)
{
    // block body
    if (dst != src)
        memmove(dst, src, src->descriptor->size);
    
    // block descriptors locate in const segment. Copy them to heap.
    static size_t bd_size = sizeof(struct Block_descriptor_1) +
        sizeof(struct Block_descriptor_2) +
        sizeof(struct Block_descriptor_3);
    struct Block_descriptor_1 *heap_bd = (struct Block_descriptor_1 *)g_malloc0(bd_size);
    // never freed
    
    uint8_t *u8d = (uint8_t *)heap_bd;
    // bd1
    memcpy(u8d, src->descriptor, sizeof(struct Block_descriptor_1));
    u8d += sizeof(struct Block_descriptor_1);
    // bd2
    struct Block_descriptor_2 *src_bd2 = block_get_descriptor2(src);
    if (src_bd2) {
        memcpy(u8d, src_bd2, sizeof(struct Block_descriptor_2));
        u8d += sizeof(struct Block_descriptor_2);
    }
    // bd3
    struct Block_descriptor_3 *src_bd3 = block_get_descriptor3(src);
    if (src_bd3) {
        memcpy(u8d, src_bd3, sizeof(struct Block_descriptor_3));
        // u8d += sizeof(struct Block_descriptor_3);
    }
    
    dst->descriptor = heap_bd;
}

static void
my_block_byref_copy(struct Block_byref *dst, struct Block_byref *src)
{
    if (dst != src)
        memmove(dst, src, src->size);
}

// this is for our blocks on stack
static struct Block_layout **
save_block_src_dst_ptr_to_stack(uint8_t *u8dst, struct Block_layout *src)
{
    // save pointer to original block
    struct Block_layout **src_ptr = (struct Block_layout **)u8dst;
    *src_ptr = src;
    
    // save pointer to copied block
    u8dst += sizeof(struct Block_layout *);
    struct Block_layout **dst_ptr = (struct Block_layout **)u8dst;
    
    // point u8dst to copied block
    u8dst += sizeof(struct Block_layout *);
    struct Block_layout *dst = (struct Block_layout *)u8dst;
    *dst_ptr = dst;
    
    // return pointer to copied block. Caller can use it to set the real dst.
    return dst_ptr;
}

static struct Block_byref **
save_block_byref_src_dst_ptr_to_stack(uint8_t *u8dst, struct Block_byref *src)
{
    struct Block_byref **src_ptr = (struct Block_byref **)u8dst;
    *src_ptr = src;
    
    u8dst += sizeof(struct Block_byref *);
    struct Block_byref **dst_ptr = (struct Block_byref **)u8dst;
    
    u8dst += sizeof(struct Block_byref *);
    struct Block_byref *dst = (struct Block_byref *)u8dst;
    *dst_ptr = dst;
    
    return dst_ptr;
}

/**
 * block_copy_to_stack_or_heap:
 * @param u8dst: points to stack
 * @return: the copied block
 */
DEPRECATED_MSG_ATTRIBUTE("this function is for reference only")
struct Block_layout *
block_copy_to_stack_or_heap(uint8_t *u8dst, struct Block_layout *src)
{
    // the src and dst pointer are always saved to stack, even though src is nil
    struct Block_layout **dst_ptr = save_block_src_dst_ptr_to_stack(u8dst, src);
    
    if (NULL == src)
        return NULL;
    
    // A block may be referenced multiple times. Check before malloc.
    // FIXME: block may originally contains x86_64 callback. A hash table is
    // more accurate.
    if (!need_emulation((uintptr_t)src->invoke)) {
        *dst_ptr = src;
        return src;
    }
    
    // there are other kinds of blocks defined in Block_private.h
    // _NSConcreteMallocBlock
    // _NSConcreteAutoBlock
    // _NSConcreteFinalizingBlock
    // _NSConcreteWeakBlockVariable
    // Based on libclosure-74, only _NSConcreteMallocBlock is in use.
    
    struct Block_layout *dst = NULL;
    if (&_NSConcreteStackBlock == src->isa) {
        // A stack block may be copied by callee. So the descriptor should live
        // in heap. `my_block_copy` will handle this.
        dst = src;
        *dst_ptr = dst;
    } else if (&_NSConcreteGlobalBlock == src->isa) {
        dst = (struct Block_layout *)g_malloc0(block_get_size_for_heap_copy(src));
        // never freed
        *dst_ptr = dst;
    } else {
        // _NSConcreteMallocBlock
        dst = src;
        *dst_ptr = dst;
    }
    
    my_block_copy(dst, src);
    return dst;
}

struct Block_layout *
block_copy_to_heap_or_not(struct Block_layout *src)
{
    if (NULL == src)
        return NULL;
    
    if (!need_emulation((uintptr_t)src->invoke)) {
        return src;
    }
    
    struct Block_layout *dst = NULL;
    if (&_NSConcreteGlobalBlock == src->isa) {
        dst = (struct Block_layout *)g_malloc0(block_get_size_for_heap_copy(src));
        // never freed
    } else {
        dst = src;
    }
    
    my_block_copy(dst, src);
    return dst;
}

/**
 * block_update_from_stack:
 * update original block (e.g., flags)
 * @param ptr_orig: points to stack
 * @return: the copied block
 */
DEPRECATED_MSG_ATTRIBUTE("this function is for reference only")
struct Block_layout *
block_update_from_stack(uint8_t *ptr_orig)
{
    // we already saved orig block pointer
    struct Block_layout *orig = *(struct Block_layout **)ptr_orig;
    if (NULL == orig)
        return NULL;
    
    uint8_t *ptr_copied = ptr_orig + sizeof(struct Block_layout *);
    struct Block_layout *copied = *(struct Block_layout **)ptr_copied;
    
    block_update_from_copied(copied, orig);
    return copied;
}

void
block_set_copy_dispose(struct Block_layout *block, uint8_t *copy, uint8_t *dispose)
{
    struct Block_descriptor_2 *bd2 = block_get_descriptor2(block);
    if (!bd2)
        return;
    if (bd2->copy)
        bd2->copy = (BlockCopyFunction)copy;
    if (bd2->dispose)
        bd2->dispose = (BlockDisposeFunction)dispose;
    return;
}

DEPRECATED_MSG_ATTRIBUTE("this function is for reference only")
struct Block_byref *
block_byref_copy_to_stack_or_heap(uint8_t *u8dst, struct Block_byref *src)
{
    struct Block_byref **dst_ptr = save_block_byref_src_dst_ptr_to_stack(u8dst, src);
    
    if (NULL == src)
        return NULL;
    
    BlockByrefKeepFunction keep;
    BlockByrefDestroyFunction destroy;
    block_byref_get_keep_destroy(src, &keep, &destroy);
    if (!need_emulation((uintptr_t)keep)) {
        *dst_ptr = src;
        return src;
    }
    
    struct Block_byref *dst = NULL;
    // ref: _Block_byref_copy
    if ((src->forwarding->flags & BLOCK_REFCOUNT_MASK) == 0) {
        // src points to stack
        dst = src;
        *dst_ptr = dst;
    } else {
        // src points to heap
        dst = src;
        *dst_ptr = dst;
    }
    
    my_block_byref_copy(dst, src);
    return dst;
}

struct Block_byref *
block_byref_copy_to_heap_or_not(struct Block_byref *src)
{
    return src;
}

DEPRECATED_MSG_ATTRIBUTE("this function is for reference only")
struct Block_byref *
block_byref_update_from_stack(uint8_t *ptr_orig)
{
    struct Block_byref *orig = *(struct Block_byref **)ptr_orig;
    if (NULL == orig)
        return NULL;
    
    uint8_t *ptr_copied = ptr_orig + sizeof(struct Block_byref *);
    struct Block_byref *copied = *(struct Block_byref **)ptr_copied;
    
    block_byref_update_from_copied(copied, orig);
    return copied;
}

static struct Block_byref_2 *
block_byref_get_byref2(struct Block_byref *bb)
{
    if (!(bb->flags & BLOCK_BYREF_HAS_COPY_DISPOSE))
        return NULL;
    uint8_t *desc = (uint8_t *)bb;
    desc += sizeof(struct Block_byref);
    return (struct Block_byref_2 *)desc;
}

void
block_byref_get_keep_destroy(struct Block_byref *bb,
                             BlockByrefKeepFunction *keep,
                             BlockByrefDestroyFunction *destroy)
{
    struct Block_byref_2 *bb2 = block_byref_get_byref2(bb);
    if (!bb2) {
        *keep = NULL;
        *destroy = NULL;
        return;
    }
    *keep = bb2->byref_keep;
    *destroy = bb2->byref_destroy;
}

void block_byref_set_keep_destroy(struct Block_byref *bb,
                                  uint8_t *keep,
                                  uint8_t *destroy)
{
    struct Block_byref_2 *bb2 = block_byref_get_byref2(bb);
    if (!bb2)
        return;
    if (bb2->byref_keep)
        bb2->byref_keep = (BlockByrefKeepFunction)keep;
    if (bb2->byref_destroy)
        bb2->byref_destroy = (BlockByrefDestroyFunction)destroy;
    return;
}

void
init_block_system(const struct MACH_HEADER *header)
{
    // stub for now.
}
