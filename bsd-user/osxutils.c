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
#include "tcg/tcg.h"

/*
 GetThreadId for OSX. Can you believe that there is no
 native support?
 */
uint64_t GetThreadID(pthread_t thread)
{
    mach_port_name_t port = pthread_mach_thread_np(thread);
    
    thread_identifier_info_data_t info;
    mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
    kern_return_t kr = thread_info(port,
                                   THREAD_IDENTIFIER_INFO,
                                   (thread_info_t)&info,
                                   &info_count);
    if(kr != KERN_SUCCESS) {
        return 0;
    } else {
        return info.thread_id;
    }
}

uint64_t GetThreadID_np(mach_port_name_t thread)
{
    thread_identifier_info_data_t info;
    mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
    kern_return_t kr = thread_info(thread,
                                   THREAD_IDENTIFIER_INFO,
                                   (thread_info_t)&info,
                                   &info_count);
    if(kr != KERN_SUCCESS) {
        return 0;
    } else {
        return info.thread_id;
    }
}

CPUArchState *
get_env_by_thread(thread_t thread)
{
    uint64_t tid = GetThreadID_np(thread);
    if(0 == tid) {
        return NULL;
    }
    
    CPUState *some_cpu;
    CPUArchState *the_env = NULL;
    
    cpu_list_lock();
    CPU_FOREACH(some_cpu) {
        if(some_cpu->thread_id == tid) {
            the_env = some_cpu->env_ptr;
            break;
        }
    }
    cpu_list_unlock();

    return the_env;
}

CPUArchState *
get_env_by_pthread(pthread_t thread)
{
    uint64_t tid = GetThreadID(thread);
    if(0 == tid) {
        return NULL;
    }
    
    CPUState *some_cpu;
    CPUArchState *the_env = NULL;
    
    cpu_list_lock();
    CPU_FOREACH(some_cpu) {
        if(some_cpu->thread_id == tid) {
            the_env = some_cpu->env_ptr;
            break;
        }
    }
    cpu_list_unlock();

    return the_env;
}

extern int cpu_list_trylock(void);
extern int mmap_trylock(void);
extern void mmap_unlock(void);
extern int trylock_objc(void);
extern void unlock_objc(void);

static inline int tb_ctx_qht_trylock()
{
    struct qht *ht = &tb_ctx.htable;
    if (ht->mode & QHT_MODE_RAW_MUTEXES) {
        return qemu_mutex_trylock__raw(&(ht)->lock);
    }
    return qemu_mutex_trylock(&(ht)->lock);
}

static inline void tb_ctx_qht_unlock()
{
    struct qht *ht = &tb_ctx.htable;
    qemu_mutex_unlock(&ht->lock);
}

int trylock_all_qemu_lock()
{
    if (0 != cpu_list_trylock()) {
        return 1;
    }
    
    if (0 != mmap_trylock()) {
        cpu_list_unlock();
        return 1;
    }
    
    if (0 != tb_ctx_qht_trylock()) {
        mmap_unlock();
        cpu_list_unlock();
        return 1;
    }
    
    if (0 != trylock_objc()) {
        tb_ctx_qht_unlock();
        mmap_unlock();
        cpu_list_unlock();
        return 1;
    }
    
    return 0;
}

void unlock_all_qemu_lock()
{
    unlock_objc();
    tb_ctx_qht_unlock();
    mmap_unlock();
    cpu_list_unlock();
}
