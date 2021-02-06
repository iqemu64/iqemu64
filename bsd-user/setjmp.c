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

#define JMP_r19_20      0x00
#define JMP_r21_22      0x10
#define JMP_r23_24      0x20
#define JMP_r25_26      0x30
#define JMP_r27_28      0x40
#define JMP_fp_lr       0x50
#define JMP_sp_rsvd     0x60    /* second field is reserved/unused */
#define JMP_d8_d9       0x70
#define JMP_d10_d11     0x80
#define JMP_d12_d13     0x90
#define JMP_d14_d15     0xA0
#define JMP_sig         0xB0
#define JMP_sigflag     0xB8

int _arm_setjmp(uint64_t *buf, CPUArchState *env)
{
    memcpy(&buf[JMP_r19_20 >> 3], &env->xregs[19], sizeof(register_t) * 13);
    buf[JMP_d8_d9 >> 3]         = env->vfp.zregs[8].d[0];
    buf[(JMP_d8_d9 >> 3) + 1]   = env->vfp.zregs[9].d[0];
    buf[JMP_d10_d11 >> 3]       = env->vfp.zregs[10].d[0];
    buf[(JMP_d10_d11 >> 3) + 1] = env->vfp.zregs[11].d[0];
    buf[JMP_d12_d13 >> 3]       = env->vfp.zregs[12].d[0];
    buf[(JMP_d12_d13 >> 3) + 1] = env->vfp.zregs[13].d[0];
    buf[JMP_d14_d15 >> 3]       = env->vfp.zregs[14].d[0];
    buf[(JMP_d14_d15 >> 3) + 1] = env->vfp.zregs[15].d[0];
    
    return 0;
}

int arm_sigsetjmp(uint64_t *buf, int sigmask, CPUArchState *env)
{
    buf[JMP_sigflag >> 3] = sigmask;
    if(sigmask)
        do_sigprocmask(SIG_BLOCK, NULL, (sigset_t *)&buf[JMP_sig >> 3]);
    return _arm_setjmp(buf, env);
}

int arm_setjmp(uint64_t *buf, CPUArchState *env)
{
    do_sigprocmask(SIG_BLOCK, NULL, (sigset_t *)&buf[JMP_sig >> 3]);
    return _arm_setjmp(buf, env);
}

int _arm_longjmp(uint64_t *buf, int ret_val, CPUArchState *env)
{
    memcpy(&env->xregs[19], &buf[JMP_r19_20 >> 3], sizeof(register_t) * 13);
    env->vfp.zregs[8].d[0] = buf[JMP_d8_d9 >> 3];
    env->vfp.zregs[9].d[0] = buf[(JMP_d8_d9 >> 3) + 1];
    env->vfp.zregs[10].d[0] = buf[JMP_d10_d11 >> 3];
    env->vfp.zregs[11].d[0] = buf[(JMP_d10_d11 >> 3) + 1];
    env->vfp.zregs[12].d[0] = buf[JMP_d12_d13 >> 3];
    env->vfp.zregs[13].d[0] = buf[(JMP_d12_d13 >> 3) + 1];
    env->vfp.zregs[14].d[0] = buf[JMP_d14_d15 >> 3];
    env->vfp.zregs[15].d[0] = buf[(JMP_d14_d15 >> 3) + 1];
    
    env->pc = env->xregs[30];      // not a part of restore but should do it here.
    if(ret_val == 0) {
        ret_val = 1;
    }
    
    return ret_val;
}

int arm_siglongjmp(uint64_t *buf, int ret_val, CPUArchState *env)
{
    uint64_t sigmask = buf[JMP_sigflag >> 2];
    
    if(sigmask) {
        do_sigprocmask(SIG_SETMASK, (const sigset_t *)&buf[JMP_sig >> 2], NULL);
    }
    
    return _arm_longjmp(buf, ret_val, env);
}

int arm_longjmp(uint64_t *buf, int ret_val, CPUArchState *env)
{
    do_sigprocmask(SIG_SETMASK, (const sigset_t *)&buf[JMP_sig >> 2], NULL);
    return _arm_longjmp(buf, ret_val, env);
}

//
// Export area

void call_arm_setjmp(CPUArchState *env)
{
    qemu_log("setjmp called.\n");
    env->xregs[0] = arm_setjmp((uint64_t *)env->xregs[0], env);
    
    env->pc = env->xregs[30];
}

void call__arm_setjmp(CPUArchState *env)
{
    qemu_log("_setjmp called.\n");
    env->xregs[0] = _arm_setjmp((uint64_t *)env->xregs[0], env);
    
    env->pc = env->xregs[30];
}

void call_arm_sigsetjmp(CPUArchState *env)
{
    qemu_log("sigsetjmp called.\n");
    env->xregs[0] = arm_sigsetjmp((uint64_t *)env->xregs[0], (int)env->xregs[1], env);
    
    env->pc = env->xregs[30];
}

void call_arm_longjmp(CPUArchState *env)
{
    qemu_log("longjmp called.\n");
    env->xregs[0] = arm_longjmp((uint64_t *)env->xregs[0], (int)env->xregs[1], env);
}

void call__arm_longjmp(CPUArchState *env)
{
    qemu_log("_longjmp called.\n");
    env->xregs[0] = _arm_longjmp((uint64_t *)env->xregs[0], (int)env->xregs[1], env);
}

void call_arm_siglongjmp(CPUArchState *env)
{
    qemu_log("siglongjmp called.\n");
    env->xregs[0] = arm_siglongjmp((uint64_t *)env->xregs[0], (int)env->xregs[1], env);
}
