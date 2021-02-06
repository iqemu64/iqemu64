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

#include "type-encoding/aa_reg.h"
#include "type-encoding/xx_reg.h"
#include "tcg/ABIArgInfo.h"
#include "qemu.h"
#include "bsd-user/objc/objc.h"

#define REPLACE_VALIST_WITH_ELLIPSIS 1 // sync with register_important_funcs
#define REG_SP_NUM 31
// make sure it is a multiple of 16
#define BIG_ENOUGH_SRET_SIZE 64
#define register_args tcg_dy_register_args

#define save_lr_to_stack(s) \
do { \
tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30])); \
tcg_out_push(s, TCG_REG_RAX); \
tcg_out_addi(s, TCG_REG_RSP, -8); \
} while (0)

#define update_pc_from_lr(s) \
do { \
tcg_out_addi(s, TCG_REG_RSP, 8); \
tcg_out_pop(s, TCG_REG_RAX); \
tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30])); \
tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, pc)); \
} while (0)

static
void abi_x2a_get_translation_function_pair_by_types_nolock(const char *types,
                                                           fnEntryTranslation *entry_translation,
                                                           fnExitTranslation *exit_translation);

const char *block_copy_types = "v16^v0^v8";
const char *block_dispose_types = "v8^v0";
const char *block_keep_types = "v16^?0^?8";
const char *block_destroy_types = "v8^?0";
// ret: {RAX, RDX} - {X0, X1}
// arg: 8 pointers and 8 float numbers
const char *default_method_types = "{ret=**}@0:8*16*24*32*40*48*56dddddddd";

extern void *
NSLog_ptr(void);
extern void *
NSLogv_ptr(void);
extern void *
CFStringCreateWithFormat_ptr(void);
extern void *
CFStringAppendFormat_ptr(void);
extern void *
CFStringCreateWithFormatAndArguments_ptr(void);
extern void *
CFStringAppendFormatAndArguments_ptr(void);
extern void *
CGColorConversionInfoCreateFromList_ptr(void);
extern void *
CGColorConversionInfoCreateFromListWithArguments_ptr(void);

extern
const char *nsstr_2_cstr(void *ns);

typedef void *(* MallocFn)(size_t);
typedef void (* FreeFn)(void *);

extern
const char *cfstring_dup_cstr(void *cf, MallocFn m, FreeFn f);

extern
bool parse_fmtstr(const char *str, unsigned nfixed, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern
bool parse_scanf_fmtstr(const char *str, unsigned idxOfFmt, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern
bool parse_type_codes(const char *types, unsigned nfixed, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern
bool get_fn_info_from_types(const char *str, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern
bool get_vari_fn_info_from_types(const char *str, unsigned nfixed,
                                 ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern
bool get_fn_info_from_name(const char *str, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

void iqemu_bridge_asm_log(const char *msg, const char *nameOrTypes, void *start, void *stop) {
    FILE *logfile = qemu_log_lock();
    size_t genCodeSize = stop - start;
    qemu_log("%s, %s: [size=%zu]\n", msg, nameOrTypes, genCodeSize);
    
    log_disas(start, genCodeSize);
    
    qemu_log("\n");
    qemu_log_flush();
    qemu_log_unlock(logfile);
}

// keep this code synchronized with enum aarch64_reg and CPUARMState
static inline
size_t a64_enum_2_offset(int num) {
    if (TE_S0 <= num && num <= TE_S7) {
        return offsetof(CPUARMState, vfp.zregs[num - TE_S0]);
    } else if (TE_D0 <= num && num <= TE_D7) {
        return offsetof(CPUARMState, vfp.zregs[num - TE_D0]);
    } else if (TE_Q0 <= num && num <= TE_Q7) {
        return offsetof(CPUARMState, vfp.zregs[num - TE_Q0]);
    } else if (TE_W0 <= num && num <= TE_W7) {
        return offsetof(CPUARMState, xregs[num - TE_W0]);
    } else if (TE_X0 <= num && num <= TE_X8) {
        return offsetof(CPUARMState, xregs[num - TE_X0]);
    }
    return 0;
}

// keep this code synchronized with enum x86_64_reg and TCGReg
static inline
int x64_enum_2_reg(int num) {
    switch (num) {
        case TE_RAX:
        case TE_EAX:
        case TE_AX:
        case TE_AL:
            return TCG_REG_RAX;
        case TE_RDI:
        case TE_EDI:
            return TCG_REG_RDI;
        case TE_RSI:
        case TE_ESI:
            return TCG_REG_RSI;
        case TE_RDX:
        case TE_EDX:
        case TE_DL:
            return TCG_REG_RDX;
        case TE_RCX:
        case TE_ECX:
            return TCG_REG_RCX;
        case TE_R8:
        case TE_R8D:
            return TCG_REG_R8;
        case TE_R9:
        case TE_R9D:
            return TCG_REG_R9;
        default:
            return TCG_REG_XMM0 + (num - TE_XMM0);
    }
}

// keep this code synchronized with enum aarch64_reg and CPUARMState
static inline
void *a64_enum_2_ptr(CPUARMState *env, int num) {
    if (TE_S0 <= num && num <= TE_S7) {
        return &env->vfp.zregs[num - TE_S0];
    } else if (TE_D0 <= num && num <= TE_D7) {
        return &env->vfp.zregs[num - TE_D0];
    } else if (TE_Q0 <= num && num <= TE_Q7) {
        return &env->vfp.zregs[num - TE_Q0];
    } else if (TE_W0 <= num && num <= TE_W7) {
        return &env->xregs[num - TE_W0];
    } else if (TE_X0 <= num && num <= TE_X8) {
        return &env->xregs[num - TE_X0];
    }
    return NULL;
}

// keep this code synchronized with enum x86_64_reg and register_args
static inline
void *x64_enum_2_ptr(register_args *fake_regs, int num) {
    switch (num) {
        case TE_RAX:
        case TE_EAX:
        case TE_AX:
        case TE_AL:
            return &fake_regs->rax;
        case TE_RDI:
        case TE_EDI:
            return &fake_regs->gpr[0];
        case TE_RSI:
        case TE_ESI:
            return &fake_regs->gpr[1];
        case TE_RDX:
        case TE_EDX:
        case TE_DL:
            return &fake_regs->gpr[2];
        case TE_RCX:
        case TE_ECX:
            return &fake_regs->gpr[3];
        case TE_R8:
        case TE_R8D:
            return &fake_regs->gpr[4];
        case TE_R9:
        case TE_R9D:
            return &fake_regs->gpr[5];
        default:
            return &fake_regs->sse[(num - TE_XMM0) * 2];
    }
}

#define my_mem_cp memcpy

static void extend_arg_by_loc_info(void *dst, unsigned locInfo) {
    if (locInfo == TE_SExt8_32) {
        int8_t x = *(int8_t *)dst;
        int32_t y = x;
        memcpy(dst, &y, 4);
    } else if (locInfo == TE_SExt16_32) {
        int16_t x = *(int16_t *)dst;
        int32_t y = x;
        memcpy(dst, &y, 4);
    } else if (locInfo == TE_ZExt8_32) {
        uint8_t x = *(uint8_t *)dst;
        uint32_t y = x;
        memcpy(dst, &y, 4);
    } else if (locInfo == TE_ZExt16_32) {
        uint16_t x = *(uint16_t *)dst;
        uint32_t y = x;
        memcpy(dst, &y, 4);
    }
}

static void extend_ret_by_reg_loc_info(void *dst, unsigned x64Reg, unsigned locInfo) {
    if (x64Reg == TE_AL || x64Reg == TE_DL) {
        if (locInfo == TE_SExt8_32) {
            int8_t x = *(int8_t *)dst;
            int32_t y = x;
            memcpy(dst, &y, 4);
        } else {
            assert(locInfo == TE_ZExt8_32 || locInfo == TE_Full);
            uint8_t x = *(uint8_t *)dst;
            uint32_t y = x;
            memcpy(dst, &y, 4);
        }
    } else if (x64Reg == TE_AX) {
        if (locInfo == TE_SExt16_32) {
            int16_t x = *(int16_t *)dst;
            int32_t y = x;
            memcpy(dst, &y, 4);
        } else {
            assert(locInfo == TE_ZExt16_32 || locInfo == TE_Full);
            uint16_t x = *(uint16_t *)dst;
            uint32_t y = x;
            memcpy(dst, &y, 4);
        }
    }
}

static void
abi_dy_a2x_entry_trans_internal(CPUARMState *env,
                                ABIFnInfo *aaInfo, ABIFnInfo *xxInfo,
                                char *arg_space, char *sret,
                                char * const AARCH64_SP) {
    register_args * const fake_regs = (register_args *)arg_space;
    char * const fake_stack = arg_space + sizeof(register_args);
    // check return value first
    ABIArgInfo xxRetInfo = xxInfo->rinfo, aaRetInfo = aaInfo->rinfo;
    switch (te_is_indirect(xxRetInfo) * 2 + te_is_indirect(aaRetInfo)) {
        case 0b01:
            puts("direct 2 sret just happened!\n");
            break;
        case 0b10:
            *(uint64_t *)x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, 0)) = (uint64_t)sret;
            break;
        case 0b11:
            my_mem_cp(x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, 0)),
                      &env->xregs[8], 8);
            break;
        default:
            break;
    }
    // map arguments
    for (unsigned i = 0, e = xxInfo->nargs; i != e; ++i) {
        ABIArgInfo xxArgInfo = xxInfo->ainfo[i], aaArgInfo = aaInfo->ainfo[i];
        unsigned xxRegCnt = te_num_of_reg(xxArgInfo), aaRegCnt = te_num_of_reg(aaArgInfo);
        
        // the argument may be of kind "Ignore"
        if ((!te_is_mem(xxArgInfo) && 0 == xxRegCnt) || (!te_is_mem(aaArgInfo) && 0 == aaRegCnt))
            continue;
        
        switch ((te_is_indirect(xxArgInfo) << 3) +
                (te_is_mem(xxArgInfo)      << 2) +
                (te_is_indirect(aaArgInfo) << 1) +
                (te_is_mem(aaArgInfo))) {
            case 0b0000: // direct in reg - direct in reg
            {
                if (xxRegCnt == aaRegCnt) {
                    // map them one by one
                    for (unsigned j = 0; j < xxRegCnt; ++j) {
                        if (TE_XMM0 <= te_get_reg(xxArgInfo, j) && te_get_reg(xxArgInfo, j) <= TE_XMM7) {
                            my_mem_cp(x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, j)),
                                      a64_enum_2_ptr(env, te_get_reg(aaArgInfo, j)),
                                      16);
                        } else {
                            void *dst = x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, j));
                            my_mem_cp(dst,
                                      a64_enum_2_ptr(env, te_get_reg(aaArgInfo, j)),
                                      8);
                            unsigned locInfo = te_get_reg_locinfo(xxArgInfo, j);
                            extend_arg_by_loc_info(dst, locInfo);
                        }
                    }
                } else if (xxRegCnt < aaRegCnt) {
                    if (1 == xxRegCnt && 2 == aaRegCnt) {
                        assert(TE_XMM0 <= te_get_reg(xxArgInfo, 0) && te_get_reg(xxArgInfo, 0) <= TE_XMM7);
                        assert(te_get_reg(aaArgInfo, 0) + 1 == te_get_reg(aaArgInfo, 1));
                        char *xPtr = x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, 0));
                        my_mem_cp(xPtr, a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 0)), 4);
                        my_mem_cp(xPtr + 4, a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 1)), 4);
                    } else if (2 == xxRegCnt) {
                        assert(TE_XMM0 <= te_get_reg(xxArgInfo, 0) && te_get_reg(xxArgInfo, 0) <= TE_XMM6);
                        assert(te_get_reg(xxArgInfo, 0) + 1 == te_get_reg(xxArgInfo, 1));
                        assert(3 == aaRegCnt || 4 == aaRegCnt);
                        assert(te_get_reg(aaArgInfo, 0) + 1 == te_get_reg(aaArgInfo, 1));
                        assert(te_get_reg(aaArgInfo, 1) + 1 == te_get_reg(aaArgInfo, 2));
                        char *xPtr = x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, 0));
                        my_mem_cp(xPtr, a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 0)), 4);
                        my_mem_cp(xPtr + 4, a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 1)), 4);
                        xPtr = x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, 1));
                        my_mem_cp(xPtr, a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 2)), 4);
                        if (4 == aaRegCnt) {
                            my_mem_cp(xPtr + 4, a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 3)), 4);
                        }
                    } else {
                        assert(0);
                    }
                } else {
                    assert(0 && "xxRegCnt > aaRegCnt");
                }
                break;
            }
            case 0b0001: // direct in reg - direct in mem
            {
                unsigned offset = te_get_mem(aaArgInfo);
                if (xxRegCnt * 16 == te_get_arg_size(aaArgInfo)) {
                    for (unsigned j = 0; j < xxRegCnt; ++j) {
                        my_mem_cp(x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, j)),
                                  AARCH64_SP + offset,
                                  16);
                        offset += 16;
                    }
                } else {
                    for (unsigned j = 0; j < xxRegCnt; ++j) {
                        void *dst = x64_enum_2_ptr(fake_regs, te_get_reg(xxArgInfo, j));
                        my_mem_cp(dst,
                                  AARCH64_SP + offset,
                                  8);
                        unsigned locInfo = te_get_reg_locinfo(xxArgInfo, j);
                        extend_arg_by_loc_info(dst, locInfo);
                        offset += 8;
                    }
                }
                break;
            }
            case 0b0010: // direct in reg - indirect in reg
            case 0b0011: // direct in reg - indirect in mem
                assert(0 && "impossible");
                break;
            case 0b0100: // direct in mem - direct in reg
            {
                unsigned offset = te_get_mem(xxArgInfo);
                if (aaRegCnt * 16 == te_get_arg_size(xxArgInfo)) {
                    for (unsigned j = 0; j < aaRegCnt; ++j) {
                        my_mem_cp(fake_stack + offset,
                                  a64_enum_2_ptr(env, te_get_reg(aaArgInfo, j)),
                                  16);
                        offset += 16;
                    }
                } else {
                    for (unsigned j = 0; j < aaRegCnt; ++j) {
                        void *dst = fake_stack + offset;
                        my_mem_cp(dst,
                                  a64_enum_2_ptr(env, te_get_reg(aaArgInfo, j)),
                                  8);
                        unsigned locInfo = te_get_mem_locinfo(xxArgInfo);
                        extend_arg_by_loc_info(dst, locInfo);
                        offset += 8;
                    }
                }
                break;
            }
            case 0b0101: // direct in mem - direct in mem
            {
                unsigned aa_offset = te_get_mem(aaArgInfo);
                unsigned xx_offset = te_get_mem(xxArgInfo);
                const unsigned arg_size = te_get_arg_size(aaArgInfo);
                assert(arg_size == te_get_arg_size(xxArgInfo));
                void *dst = fake_stack + xx_offset;
                my_mem_cp(dst, AARCH64_SP + aa_offset, arg_size);
                
                if (arg_size <= 2) {
                    unsigned locInfo = te_get_mem_locinfo(xxArgInfo);
                    extend_arg_by_loc_info(dst, locInfo);
                }
                break;
            }
            case 0b0110: // direct in mem - indirect in reg
            {
                unsigned xx_offset = te_get_mem(xxArgInfo);
                unsigned arg_size = te_get_arg_size(xxArgInfo);
                uint64_t reg_val = *(uint64_t *)a64_enum_2_ptr(env, te_get_reg(aaArgInfo, 0));
                char *aPtr = (char *)reg_val;
                my_mem_cp(fake_stack + xx_offset, aPtr, arg_size);
                
                assert(arg_size > 2);
                break;
            }
            case 0b0111: // direct in mem - indirect in mem
            {
                unsigned aa_offset = te_get_mem(aaArgInfo);
                unsigned xx_offset = te_get_mem(xxArgInfo);
                unsigned arg_size = te_get_arg_size(xxArgInfo);
                uint64_t mem_val = *(uint64_t *)(AARCH64_SP + aa_offset);
                char *aPtr = (char *)mem_val;
                my_mem_cp(fake_stack + xx_offset, aPtr, arg_size);
                
                assert(arg_size > 2);
                break;
            }
            default:
                assert(0 && "only arguments in aarch64 may be passed indirectly");
                break;
        }
    }
    fake_regs->rax = xxInfo->ssecount;
}

/*
 * abi_dy_a2x_entry_translation:
 * @arg_space: A pointer to [register_args + stack]
 * @sret: A temporary space, useful when x86_64 needs sret while arm64 does not.
 *        Make sure it is big enough when you pass it in.
 */
//static
void abi_dy_a2x_entry_translation(CPUARMState *env, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, char *arg_space, char *sret) {
    char * const AARCH64_SP = (char *)env->xregs[REG_SP_NUM];
    return abi_dy_a2x_entry_trans_internal(env, aaInfo, xxInfo, arg_space, sret, AARCH64_SP);
}

//static
void abi_dy_a2x_exit_translation(CPUARMState *env, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, char *arg_space, char *sret) {
    register_args * const fake_regs = (register_args *)arg_space;
    
    ABIArgInfo xxRetInfo = xxInfo->rinfo, aaRetInfo = aaInfo->rinfo;
    unsigned xxRegCnt = te_num_of_reg(xxRetInfo), aaRegCnt = te_num_of_reg(aaRetInfo);
    
    if (xxInfo->isCxxStructor && 0 == xxRegCnt && 1 == aaRegCnt) {
        // CxxStructor in aarch64 need to return `this`
        // `this` pointer is passed in X0 (first argument) and returned in X0
        // nothing to do
        return;
    }
    if (xxInfo->isCxxStructor && 1 == xxRegCnt && 0 == aaRegCnt) {
        // CxxStructor in aarch64 does not need to return `this`
        // nothing to do
        return;
    }
    
    switch (te_is_indirect(xxRetInfo) * 2 + te_is_indirect(aaRetInfo)) {
        case 0b00: // similar to case 0b0000 when mapping arguments
        {
            if (xxRegCnt == aaRegCnt) {
                // map them one by one
                for (unsigned j = 0; j < xxRegCnt; ++j) {
                    if (TE_XMM0 <= te_get_reg(xxRetInfo, j) && te_get_reg(xxRetInfo, j) <= TE_XMM7) {
                        my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, j)),
                                  x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, j)),
                                  16);
                    } else {
                        void *dst = a64_enum_2_ptr(env, te_get_reg(aaRetInfo, j));
                        my_mem_cp(dst,
                                  x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, j)),
                                  8);
                        unsigned locInfo = te_get_reg_locinfo(xxRetInfo, j);
                        unsigned x64Reg = te_get_reg(xxRetInfo, j);
                        extend_ret_by_reg_loc_info(dst, x64Reg, locInfo);
                    }
                }
            } else if (xxRegCnt < aaRegCnt) {
                if (1 == xxRegCnt && 2 == aaRegCnt) {
                    assert(TE_XMM0 == te_get_reg(xxRetInfo, 0));
                    assert(TE_S0 == te_get_reg(aaRetInfo, 0));
                    assert(te_get_reg(aaRetInfo, 0) + 1 == te_get_reg(aaRetInfo, 1));
                    char *xPtr = (char *)x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, 0));
                    // 1st S reg
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, 0)),
                              xPtr, 4);
                    // 2nd S reg
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, 1)),
                              xPtr + 4, 4);
                } else if (2 == xxRegCnt) {
                    assert(TE_XMM0 == te_get_reg(xxRetInfo, 0));
                    assert(te_get_reg(xxRetInfo, 0) + 1 == te_get_reg(xxRetInfo, 1));
                    assert(3 == aaRegCnt || 4 == aaRegCnt);
                    assert(TE_S0 == te_get_reg(aaRetInfo, 0));
                    assert(te_get_reg(aaRetInfo, 0) + 1 == te_get_reg(aaRetInfo, 1));
                    assert(te_get_reg(aaRetInfo, 1) + 1 == te_get_reg(aaRetInfo, 2));
                    char *xPtr = (char *)x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, 0));
                    // 1st S reg
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, 0)),
                              xPtr, 4);
                    // 2nd S reg
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, 1)),
                              xPtr + 4, 4);
                    // 3rd S reg, 2nd XMM
                    xPtr = (char *)x64_enum_2_ptr(fake_regs, te_get_reg(xxRetInfo, 1));
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, 2)),
                              xPtr, 4);
                    if (4 == aaRegCnt) {
                        // 4nd S reg
                        my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, 3)),
                                  xPtr + 4, 4);
                    }
                } else {
                    assert(0);
                }
            } else {
                assert(0 && "xxRegCnt > aaRegCnt");
            }
            break;
        }
        case 0b01:
            puts("direct 2 sret just happened!\n");
            abort();
            break;
        case 0b10:
        {
            for (unsigned j = 1; j < aaRegCnt; ++j) {
                assert(te_get_reg(aaRetInfo, j - 1) + 1 == te_get_reg(aaRetInfo, j));
            }
            unsigned offset = 0;
            if (TE_D0 == te_get_reg(aaRetInfo, 0)) {
                for (unsigned j = 0; j < aaRegCnt; ++j) {
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, j)),
                              sret + offset, 8);
                    offset += 8;
                }
            } else if (TE_Q0 == te_get_reg(aaRetInfo, 0)) {
                for (unsigned j = 0; j < aaRegCnt; ++j) {
                    my_mem_cp(a64_enum_2_ptr(env, te_get_reg(aaRetInfo, j)),
                              sret + offset, 16);
                    offset += 16;
                }
            } else {
                printf("aa reg %d\n", te_get_reg(aaRetInfo, 0));
                assert(0 && "possible");
                abort();
            }
            break;
        }
        default:
            break;
    }
}

/*
 * abi_valist_a2x_prepare_args:
 * @arg_space: A pointer to [register_args + stack]
 * @sret: A temporary space, useful when x86_64 needs sret while arm64 does not.
 *        Make sure it is big enough when you pass it in.
 * Assume: All fixed arguments are passed in GPR.
 */
void abi_valist_a2x_prepare_args(CPUARMState *env, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, char *arg_space, char *sret, unsigned idxOfValist) {
    // map arguments
    assert(idxOfValist <= 7);
    char * const AARCH64_SP = (char *)env->xregs[idxOfValist];
    return abi_dy_a2x_entry_trans_internal(env, aaInfo, xxInfo, arg_space, sret, AARCH64_SP);
}

#undef my_mem_cp

#pragma mark - Special Func

typedef void (*dy_a2x_bridge_trampoline)(uint64_t pc,
                                        char * arg_space,
                                        unsigned stack_size);

static
void my_scanf_family(CPUARMState *env, unsigned idxOfFmt) {
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_scanf_fmtstr((const char *)(env->xregs[idxOfFmt]), idxOfFmt,
                            &aaInfo, &xxInfo)) {
        puts("parse scanf fmtstr failed");
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    // scanf family return int
    // uint64_t big_enough_sret[8];
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_printf_family(CPUARMState *env, unsigned idxOfFmt) {
    ABIFnInfo aaInfo, xxInfo;
    
    if (!parse_fmtstr((const char *)(env->xregs[idxOfFmt]), idxOfFmt + 1, &aaInfo,
                      &xxInfo)) {
        puts("parse format string failed");
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    // printf family return int
    // uint64_t big_enough_sret[8];
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

/*
 * x86_64: check the ABI document for details.
 *  typedef struct {
 *      unsigned int gp_offset;
 *      unsigned int fp_offset;
 *      void *overflow_arg_area; // arguments passed on the stack
 *      void *reg_save_area;
 *  } va_list[1];
 *
 * iOS: the type va_list is an alias for char * rather than for the struct type
 * specified in the generic PCS. As a result, the ellipsis-version fn and the
 * va_list-version fn have almost the same argument layout.
 */

#include <xlocale.h> // for printf_family_l

// assume the format string is right before va_list
static
void my_vprintf_family(CPUARMState *env, unsigned idxOfValist) {
    assert(idxOfValist <= 7);
    const char *format = (const char *)(env->xregs[idxOfValist - 1]);
    // stage 1: Take the function as a variadic function. Obtain its ABIFnInfo
    // from format.
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_fmtstr(format, idxOfValist, &aaInfo, &xxInfo)) {
        puts("parse format string failed");
        abort();
    }
    
    // stage 2: Prepare arguments.
    char *va_arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    abi_valist_a2x_prepare_args(env, &aaInfo, &xxInfo, va_arg_space, NULL, idxOfValist);
#if REPLACE_VALIST_WITH_ELLIPSIS
    if ((uint64_t)vprintf == env->pc)
        env->pc = (uint64_t)printf;
    else if ((uint64_t)vasprintf == env->pc)
        env->pc = (uint64_t)asprintf;
    else if ((uint64_t)vdprintf == env->pc)
        env->pc = (uint64_t)dprintf;
    else if ((uint64_t)vfprintf == env->pc)
        env->pc = (uint64_t)fprintf;
    else if ((uint64_t)vsprintf == env->pc)
        env->pc = (uint64_t)sprintf;
    else if ((uint64_t)vprintf_l == env->pc)
        env->pc = (uint64_t)printf_l;
    else if ((uint64_t)vsnprintf == env->pc)
        env->pc = (uint64_t)snprintf;
    else if ((uint64_t)vasprintf_l == env->pc)
        env->pc = (uint64_t)asprintf_l;
    else if ((uint64_t)vfprintf_l == env->pc)
        env->pc = (uint64_t)fprintf_l;
    else if ((uint64_t)vsprintf_l == env->pc)
        env->pc = (uint64_t)sprintf_l;
    else if ((uint64_t)vsnprintf_l == env->pc)
        env->pc = (uint64_t)snprintf_l;
    else if ((uint64_t)__vsnprintf_chk == env->pc)
        env->pc = (uint64_t)__snprintf_chk;
    else if ((uint64_t)__vsprintf_chk == env->pc)
        env->pc = (uint64_t)__sprintf_chk;
    else
        abort();
    
    char *arg_space = va_arg_space;
#else
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    // stage 3: Obtain real ABIFnInfo by types.
    unsigned types_size = 1 + idxOfValist + 1; // ret + nfixed
    char *types = (char *)alloca(types_size + 1);
    memset(types, '*', types_size);
    types[types_size] = '\0';
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    
    // stage 4: Initialize va_list.
    va_list vl;
    vl->gp_offset = idxOfValist * 8;
    vl->fp_offset = 6 * 8; // no fp regs are used
    vl->reg_save_area = va_arg_space;
    vl->overflow_arg_area = va_arg_space + sizeof(register_args);
    
    // stage 5: Prepare real arguments.
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    env->xregs[idxOfValist] = (uint64_t)vl;
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
#endif
    
    // stage 6: Call and exit.
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

// assume the format string is right before va_list
static
void my_vscanf_family(CPUARMState *env, unsigned idxOfValist) {
    assert(idxOfValist <= 7);
    const char *format = (const char *)(env->xregs[idxOfValist - 1]);
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_scanf_fmtstr(format, idxOfValist - 1, &aaInfo, &xxInfo)) {
        puts("parse format string failed");
        abort();
    }
    
    char *va_arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    abi_valist_a2x_prepare_args(env, &aaInfo, &xxInfo, va_arg_space, NULL, idxOfValist);
#if REPLACE_VALIST_WITH_ELLIPSIS
    if ((uint64_t)vscanf == env->pc)
        env->pc = (uint64_t)scanf;
    else if ((uint64_t)__svfscanf == env->pc)
        env->pc = (uint64_t)fscanf;
    else if ((uint64_t)vfscanf == env->pc)
        env->pc = (uint64_t)fscanf;
    else if ((uint64_t)vsscanf == env->pc)
        env->pc = (uint64_t)sscanf;
    else
        abort();
    
    char *arg_space = va_arg_space;
#else
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    unsigned types_size = 1 + idxOfValist + 1; // ret + nfixed
    char *types = (char *)alloca(types_size + 1);
    memset(types, '*', types_size);
    types[types_size] = '\0';
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    
    va_list vl;
    vl->gp_offset = idxOfValist * 8;
    vl->fp_offset = 6 * 8; // no fp regs are used
    vl->reg_save_area = va_arg_space;
    vl->overflow_arg_area = va_arg_space + sizeof(register_args);
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    env->xregs[idxOfValist] = (uint64_t)vl;
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
#endif
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

// assume the format string is right before va_list
static
void my_vcfstring_family(CPUARMState *env, unsigned idxOfValist) {
    assert(idxOfValist <= 7);
    const char *format = cfstring_dup_cstr((void *)(env->xregs[idxOfValist - 1]), g_malloc0, g_free);
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_fmtstr(format, idxOfValist, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    g_free((gpointer)format);
    
    char *va_arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    abi_valist_a2x_prepare_args(env, &aaInfo, &xxInfo, va_arg_space, NULL, idxOfValist);
#if REPLACE_VALIST_WITH_ELLIPSIS
    if ((uint64_t)CFStringCreateWithFormatAndArguments_ptr() == env->pc)
        env->pc = (uint64_t)CFStringCreateWithFormat_ptr();
    else if ((uint64_t)CFStringAppendFormatAndArguments_ptr() == env->pc)
        env->pc = (uint64_t)CFStringAppendFormat_ptr();
    else
        abort();
    
    char *arg_space = va_arg_space;
#else
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    unsigned types_size = 1 + idxOfValist + 1; // ret + nfixed
    char *types = (char *)alloca(types_size + 1);
    memset(types, '*', types_size);
    types[types_size] = '\0';
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    
    va_list vl;
    vl->gp_offset = idxOfValist * 8;
    vl->fp_offset = 6 * 8; // no fp regs are used
    vl->reg_save_area = va_arg_space;
    vl->overflow_arg_area = va_arg_space + sizeof(register_args);
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    env->xregs[idxOfValist] = (uint64_t)vl;
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
#endif
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_nslog(CPUARMState *env) {
    ABIFnInfo aaInfo, xxInfo;
    const char *cs = nsstr_2_cstr((void *)(env->xregs[0]));
    
    if (!parse_fmtstr(cs, 1, &aaInfo, &xxInfo)) {
        puts("parse nslog string failed");
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    
    // void NSLog(NSString * _Nonnull format, ...)
    // no exit translation
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_nslogv(CPUARMState *env) {
    const unsigned idxOfValist = 1;
    ABIFnInfo aaInfo, xxInfo;
    const char *fmt = nsstr_2_cstr((void *)(env->xregs[idxOfValist - 1]));
    if (!parse_fmtstr(fmt, idxOfValist, &aaInfo, &xxInfo)) {
        puts("parse nslog string failed");
        abort();
    }
    
    char *va_arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    abi_valist_a2x_prepare_args(env, &aaInfo, &xxInfo, va_arg_space, NULL, idxOfValist);
#if REPLACE_VALIST_WITH_ELLIPSIS
    env->pc = (uint64_t)NSLog_ptr();
    
    char *arg_space = va_arg_space;
#else
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    unsigned types_size = 1 + idxOfValist + 1; // ret + nfixed
    char *types = (char *)alloca(types_size + 1);
    memset(types, '*', types_size);
    types[types_size] = '\0';
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    
    va_list vl;
    vl->gp_offset = idxOfValist * 8;
    vl->fp_offset = 6 * 8; // no fp regs are used
    vl->reg_save_area = va_arg_space;
    vl->overflow_arg_area = va_arg_space + sizeof(register_args);
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    env->xregs[idxOfValist] = (uint64_t)vl;
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
#endif
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_cfstring_family(CPUARMState *env, unsigned idxOfFmt) {
    ABIFnInfo aaInfo, xxInfo;
    const char *cs = cfstring_dup_cstr((void *)(env->xregs[idxOfFmt]), g_malloc0, g_free);
    
    if (!parse_fmtstr(cs, idxOfFmt + 1, &aaInfo, &xxInfo)) {
        puts("parse CF format string failed");
        abort();
    }
    g_free((gpointer)cs);
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    // cfstring family is not struct return
    // uint64_t big_enough_sret[8];
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_c_nil_terminated_vari(CPUARMState *env, unsigned nfixed) {
    // NS_REQUIRES_NIL_TERMINATION
    uint64_t *sp = (uint64_t *)env->xregs[REG_SP_NUM];
    unsigned nvari = 1; // at least one nil
    while (*sp) {
        ++sp;
        ++nvari;
    }
    // construct type encoding string
    unsigned len = 1 + nfixed + nvari; // ret + fix + var
    char types[len + 1];
    memset(types, '*', len);
    types[len] = '\0';
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_vari_fn_info_from_types(types, nfixed, &aaInfo, &xxInfo)) {
        printf("parse objc types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_c_nil_terminated_valist(CPUARMState *env, unsigned idxOfValist) {
    uint64_t *sp = (uint64_t *)env->xregs[idxOfValist];
    unsigned nvari = 1; // at least one nil
    while (*sp) {
        ++sp;
        ++nvari;
    }
    unsigned len = 1 + idxOfValist + nvari; // ret + fix + var
    char types[len + 1];
    memset(types, '*', len);
    types[len] = '\0';
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_vari_fn_info_from_types(types, idxOfValist, &aaInfo, &xxInfo)) {
        printf("parse objc types %s failed\n", types);
        abort();
    }
    
    char *va_arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    abi_valist_a2x_prepare_args(env, &aaInfo, &xxInfo, va_arg_space, NULL, idxOfValist);
#if REPLACE_VALIST_WITH_ELLIPSIS
    if ((uint64_t)CGColorConversionInfoCreateFromListWithArguments_ptr == env->pc)
        env->pc = (uint64_t)CGColorConversionInfoCreateFromList_ptr();
    else
        abort();
    
    char *arg_space = va_arg_space;
#else
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    unsigned real_len = 1 + idxOfValist + 1;
    char real_types[real_len + 1];
    memset(real_types, '*', real_len);
    real_types[real_len] = '\0';
    
    if (!get_fn_info_from_types(real_types, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    
    va_list vl;
    vl->gp_offset = idxOfValist * 8;
    vl->fp_offset = 6 * 8; // no fp regs are used
    vl->reg_save_area = va_arg_space;
    vl->overflow_arg_area = va_arg_space + sizeof(register_args);
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    env->xregs[idxOfValist] = (uint64_t)vl;
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
#endif
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_objc_nil_terminated_vari(CPUARMState *env, unsigned nfixed) {
    nfixed += 2; // (id, SEL, etc...)
    // NS_REQUIRES_NIL_TERMINATION
    uint64_t *sp = (uint64_t *)env->xregs[REG_SP_NUM];
    unsigned nvari = 1; // at least one nil
    while (*sp) {
        ++sp;
        ++nvari;
    }
    // construct type encoding string
    unsigned len = 1 + nfixed + nvari; // ret + fix + var
    char types[len + 1];
    memset(types, '*', len);
    types[len] = '\0';
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_vari_fn_info_from_types(types, nfixed, &aaInfo, &xxInfo)) {
        printf("parse objc types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_objc_types_vari(CPUARMState *env, unsigned idxOfTy, unsigned nfixed) {
    nfixed += 2; // (id, SEL, etc...)
    idxOfTy += 2;
    assert(idxOfTy <= 7);
    const char *type_codes = (const char *)env->xregs[idxOfTy];
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_type_codes(type_codes, nfixed, &aaInfo, &xxInfo)) {
        printf("parse objc type codes %s failed\n", type_codes);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    // Currently, only two void functions use this handler.
    // - (void)decodeValuesOfObjCTypes:(const char *)types, ...;
    // - (void)encodeValuesOfObjCTypes:(const char *)types, ...;
    // Omit exit translation.
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_objc_fmt_str_vari(CPUARMState *env, unsigned idxOfFmt, unsigned nfixed) {
    idxOfFmt += 2; // (id, SEL, etc...)
    nfixed += 2;
    assert(idxOfFmt <= 7);
    const char *fmt = nsstr_2_cstr((void *)env->xregs[idxOfFmt]);
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_fmtstr(fmt, nfixed, &aaInfo, &xxInfo)) {
        printf("parse objc fmt string <%s> failed\n", fmt);
        abort();
    }
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static
void my_objc_fmt_str_valist(CPUARMState *env, unsigned idxOfFmt, unsigned idxOfValist) {
    idxOfFmt += 2; // (id, SEL, etc...)
    idxOfValist += 2;
    assert(idxOfFmt <= 7);
    const char *fmt = nsstr_2_cstr((void *)(env->xregs[idxOfFmt]));
    ABIFnInfo aaInfo, xxInfo;
    if (!parse_fmtstr(fmt, idxOfValist, &aaInfo, &xxInfo)) {
        puts("parse fmt failed");
        abort();
    }
    
    char *va_arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    abi_valist_a2x_prepare_args(env, &aaInfo, &xxInfo, va_arg_space, NULL, idxOfValist);
#if REPLACE_VALIST_WITH_ELLIPSIS
    if ((uint64_t)NSExpression_expressionWithFormat_arguments() == env->pc)
        env->pc = (uint64_t)NSExpression_expressionWithFormat();
    else if ((uint64_t)NSPredicate_predicateWithFormat_arguments() == env->pc)
        env->pc = (uint64_t)NSPredicate_predicateWithFormat();
    else if ((uint64_t)NSString_initWithFormat_arguments() == env->pc)
        env->pc = (uint64_t)NSString_initWithFormat();
    else if ((uint64_t)NSString_initWithFormat_locale_arguments() == env->pc)
        env->pc = (uint64_t)NSString_initWithFormat_locale();
    else if ((uint64_t)NSString_deferredLocalizedIntentsStringWithFormat_fromTable_arguments() == env->pc)
        env->pc = (uint64_t)NSString_deferredLocalizedIntentsStringWithFormat_fromTable();
    else if ((uint64_t)NSException_raise_format_arguments() == env->pc)
        env->pc = (uint64_t)NSException_raise_format();
    else
        abort();
    
    char *arg_space = va_arg_space;
#else
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    unsigned types_size = 1 + idxOfValist + 1; // ret + nfixed
    char *types = (char *)alloca(types_size + 1);
    memset(types, '*', types_size);
    types[types_size] = '\0';
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        puts("parse types failed");
        abort();
    }
    
    va_list vl;
    vl->gp_offset = idxOfValist * 8;
    vl->fp_offset = 6 * 8; // no fp regs are used
    vl->reg_save_area = va_arg_space;
    vl->overflow_arg_area = va_arg_space + sizeof(register_args);
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    env->xregs[idxOfValist] = (uint64_t)vl;
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
#endif
    
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

typedef int
(*aURenderCallback)(void        *inRefCon,
                    unsigned    *ioActionFlags,
                    const void  *inTimeStamp,
                    unsigned     inBusNumber,
                    unsigned     inNumberFrames,
                    void        *ioData);

typedef struct aURenderCallbackStruct {
    aURenderCallback inputProc;
    void            *inputProcRefCon;
} aURenderCallbackStruct;

void my_AudioUnitSetProperty(CPUARMState *env) {
    /*
     extern OSStatus
     AudioUnitSetProperty(AudioUnit               inUnit,
                          AudioUnitPropertyID     inID,
                          AudioUnitScope          inScope,
                          AudioUnitElement        inElement,
                          const void * __nullable inData,
                          UInt32                  inDataSize);
     */
    const char *fnName = "_AudioUnitSetProperty";
    const unsigned kaudioUnitProperty_SetRenderCallback = 23;
    
    unsigned audioUnitPropertyID = (unsigned)env->xregs[1];
    if (audioUnitPropertyID == kaudioUnitProperty_SetRenderCallback) {
        // register callback
        aURenderCallbackStruct *ptrToCbSt = (aURenderCallbackStruct *)env->xregs[4];
        aURenderCallback cbfun = ptrToCbSt->inputProc;
        aURenderCallback trmpl = (aURenderCallback)callback_query_and_add_trampoline((void *)cbfun, "i***II*");
        // replace
        ptrToCbSt->inputProc = trmpl;
    }
    
    // normal translation
    ABIFnInfo aaInfo, xxInfo;
    if (!get_fn_info_from_name(fnName, &aaInfo, &xxInfo)) {
        qemu_log("get_fn_info_from_name failed %s\n", fnName);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

static void tcg_register_block_byref(struct Block_byref *bb, int flags);
struct Block_layout *tcg_copy_replace_blocks(struct Block_layout *src);
static struct Block_byref *tcg_copy_replace_block_byref(struct Block_byref *src);

void my__Block_object_assign(CPUARMState *env) {
    const char *fnName = "__Block_object_assign";
    const unsigned idxByref = 1;
    const unsigned idxFlag = 2;
    uint8_t *src = (uint8_t *)env->xregs[idxByref];
    int flags = (int)env->xregs[idxFlag];
    
#if BLOCK_HAS_CALLBACK_TRAMPOLINE == 0
    tcg_register_block_byref((struct Block_byref *)src, flags);
#else
    flags = flags & BLOCK_ALL_COPY_DISPOSE_FLAGS;
    if (BLOCK_FIELD_IS_BLOCK == flags) {
        struct Block_layout *src_blk = (struct Block_layout *)src;
        struct Block_layout *dst_blk = tcg_copy_replace_blocks(src_blk);
        env->xregs[idxByref] = (uint64_t)dst_blk;
    } else if ((BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK) == flags ||
               BLOCK_FIELD_IS_BYREF == flags) {
        struct Block_byref *src_byref = (struct Block_byref *)src;
        struct Block_byref *dst_byref = tcg_copy_replace_block_byref(src_byref);
        env->xregs[idxByref] = (uint64_t)dst_byref;
    }
#endif
    
    // normal translation
    ABIFnInfo aaInfo, xxInfo;
    if (!get_fn_info_from_name(fnName, &aaInfo, &xxInfo)) {
        qemu_log("get_fn_info_from_name failed %s\n", fnName);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

void my_class_replaceMethod(CPUARMState *env) {
    const char *fnName = "_class_replaceMethod";
    void *callback = (void *)env->xregs[2];
    // do we need to duplicate the types?
    const char *callback_types = (char *)env->xregs[3];
    
    env->xregs[2] = (uint64_t)callback_query_and_add_trampoline(callback, callback_types);
    
    // common routine
    ABIFnInfo aaInfo, xxInfo;
    if (!get_fn_info_from_name(fnName, &aaInfo, &xxInfo)) {
        qemu_log("get_fn_info_from_name failed %s\n", fnName);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

void my_fcntl(CPUARMState *env) {
    unsigned nfixed = 2;
    char types[] = "iii*";
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_vari_fn_info_from_types(types, nfixed, &aaInfo, &xxInfo)) {
        printf("parse types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

void my_ioctl(CPUARMState *env) {
    unsigned nfixed = 2;
    char types[] = "iiL*";
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_vari_fn_info_from_types(types, nfixed, &aaInfo, &xxInfo)) {
        printf("parse types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

void my_open(CPUARMState *env) {
    unsigned nfixed = 2;
    const char *types = NULL;
    ABIFnInfo aaInfo, xxInfo;
    
    int flags = (int)env->xregs[1];
    if (flags & O_CREAT) {
        types = "i*ii";
    } else {
        types = "i*i";
    }
    if (!get_vari_fn_info_from_types(types, nfixed, &aaInfo, &xxInfo)) {
        printf("parse types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

void my_sem_open(CPUARMState *env) {
    unsigned nfixed = 2;
    char types[] = "**iiI";
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_vari_fn_info_from_types(types, nfixed, &aaInfo, &xxInfo)) {
        printf("parse types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
}

void my_NSObject_methodForSelector(CPUARMState *env) {
    Class cls = object_getClass((id)(void *)env->xregs[0]);
    Method m = class_getClassMethod(cls, (SEL)env->xregs[2]);
    if (m == NULL)
        m = class_getInstanceMethod(cls, (SEL)env->xregs[2]);
    
    // - (IMP)methodForSelector:(SEL)aSelector;
    char types[] = "*24@0:8:16";
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        printf("parse types %s failed\n", types);
        abort();
    }
    
    char *arg_space = (char *)alloca(sizeof(register_args) + xxInfo.bytes);
    
    abi_dy_a2x_entry_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    dy_a2x_bridge_trampoline call_x64 = tcg_ctx->code_gen_dy_call_x64_trampoline;
    call_x64(env->pc, arg_space, ALIGN2POW(xxInfo.bytes, 16));
    abi_dy_a2x_exit_translation(env, &aaInfo, &xxInfo, arg_space, NULL);
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    // register the returned IMP
    if (m != NULL) {
        env->xregs[0] = (uint64_t)callback_query_and_add_trampoline((void *)env->xregs[0], m->method_types);
    }
}

#pragma mark - Special Func Code Gen

uint8_t *tcg_target_v_code_gen(TCGContext *s, tcg_insn_unit *invoke)
{
    code_gen_start(s);
    uint8_t *begin = s->code_ptr;
    save_lr_to_stack(s);
    
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RDI, TCG_AREG0);
    
    tcg_out_call(s, invoke);
    
    update_pc_from_lr(s);
    // general tb retn
    tcg_out_jmp(s, s->code_gen_quick_tbcache);
    
    code_gen_finalize(s);
    return begin;
}

static inline
uint8_t *tcg_target_Q_code_gen(TCGContext *s, tcg_insn_unit *invoke, uint64_t a0)
{
    code_gen_start(s);
    uint8_t *begin = s->code_ptr;
    save_lr_to_stack(s);
    
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RDI, TCG_AREG0);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RSI, a0);
    
    tcg_out_call(s, invoke);
    
    update_pc_from_lr(s);
    // general tb retn
    tcg_out_jmp(s, s->code_gen_quick_tbcache);
    
    code_gen_finalize(s);
    return begin;
}

static inline
uint8_t *tcg_target_QQ_code_gen(TCGContext *s, tcg_insn_unit *invoke, uint64_t a0, uint64_t a1)
{
    code_gen_start(s);
    uint8_t *begin = s->code_ptr;
    save_lr_to_stack(s);
    
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RDI, TCG_AREG0);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RSI, a0);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RDX, a1);
    
    tcg_out_call(s, invoke);
    
    update_pc_from_lr(s);
    // general tb retn
    tcg_out_jmp(s, s->code_gen_quick_tbcache);
    
    code_gen_finalize(s);
    return begin;
}

uint8_t *tcg_printf_family(TCGContext *s, unsigned idxOfFmt)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_printf_family, idxOfFmt);
}

uint8_t *tcg_scanf_family(TCGContext *s, unsigned idxOfFmt)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_scanf_family, idxOfFmt);
}

uint8_t *tcg_nslog(TCGContext *s)
{
    return tcg_target_v_code_gen(s, (tcg_insn_unit *)my_nslog);
}

uint8_t *tcg_nslogv(TCGContext *s)
{
    return tcg_target_v_code_gen(s, (tcg_insn_unit *)my_nslogv);
}

uint8_t *tcg_cfstring(TCGContext *s, unsigned idxOfFmt)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_cfstring_family, idxOfFmt);
}

uint8_t *tcg_vprintf_family(TCGContext *s, unsigned idxOfValist)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_vprintf_family, idxOfValist);
}

uint8_t *tcg_vscanf_family(TCGContext *s, unsigned idxOfValist)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_vscanf_family, idxOfValist);
}

uint8_t *tcg_vcfstring(TCGContext *s, unsigned idxOfValist)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_vcfstring_family, idxOfValist);
}

uint8_t *tcg_c_nil_terminated_vari(TCGContext *s, unsigned nfixed)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_c_nil_terminated_vari, nfixed);
}

uint8_t *tcg_c_nil_terminated_valist(TCGContext *s, unsigned idxOfValist)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_c_nil_terminated_valist, idxOfValist);
}

uint8_t *tcg_objc_nil_terminated_vari(TCGContext *s, unsigned nfixed)
{
    return tcg_target_Q_code_gen(s, (tcg_insn_unit *)my_objc_nil_terminated_vari, nfixed);
}

uint8_t *tcg_objc_types_vari(TCGContext *s, unsigned idxOfTy, unsigned nfixed)
{
    return tcg_target_QQ_code_gen(s, (tcg_insn_unit *)my_objc_types_vari, idxOfTy, nfixed);
}

uint8_t *tcg_objc_fmt_str_vari(TCGContext *s, unsigned idxOfFmt, unsigned nfixed)
{
    return tcg_target_QQ_code_gen(s, (tcg_insn_unit *)my_objc_fmt_str_vari, idxOfFmt, nfixed);
}

uint8_t *tcg_objc_fmt_str_valist(TCGContext *s, unsigned idxOfFmt, unsigned idxOfValist)
{
    return tcg_target_QQ_code_gen(s, (tcg_insn_unit *)my_objc_fmt_str_valist, idxOfFmt, idxOfValist);
}

uint8_t *tcg_gen_callback_trampoline(TCGContext *s, void *cb, const char *types)
{
    uint64_t callback = (uint64_t)cb;
    
    if (0 == callback)
        return NULL;
    
    uint8_t *begin;
    if (need_emulation_nolock(callback)) {
        fnEntryTranslation entry_translation = NULL;
        fnExitTranslation exit_translation = NULL;
        abi_x2a_get_translation_function_pair_by_types_nolock(types,
                                                              &entry_translation,
                                                              &exit_translation);
        
        code_gen_start(s);
        begin = s->code_ptr;
        
        tcg_out8(s, OPC_JMP_short);
        tcg_out8(s, sizeof(uint32_t) + sizeof(uint64_t));
        tcg_out32(s, CALLBACK_TRAMPOLINE_MARK);
        // store the original callback in case there is an arm->arm callback
        tcg_out64(s, callback);
        
        // We replace original callback with this 'code_ptr'. So this 'code_ptr'
        // will receive params passed to original callback function.
        // DO NOT do anything that may corrupt the parameter registers.
        
        // See TCGContext::code_gen_xloop_trampoline for details.
        // We cannot directly push callback onto stack
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_R10, (tcg_target_ulong)callback);
        tcg_out_push(s, TCG_REG_R10);
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_R10, (tcg_target_ulong)entry_translation);
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_R11, (tcg_target_ulong)exit_translation);
        tcg_out_jmp(s, (tcg_insn_unit *)s->code_gen_xloop_trampoline);
        
        code_gen_finalize(s);
    } else {
        // register this callback in case it's an unnamed symbol
        tcg_register_callback_as_type_nolock(callback, types);
        begin = (uint8_t *)callback;
    }
    return begin;
}

void tcg_register_blocks(struct Block_layout *block) {
    if (NULL == block)
        return;
    BlockInvokeFunction invoke = block_get_invoke(block);
    const char *signature = block_get_signature(block);
    if (invoke) {
        if (NULL == signature) {
            signature = default_method_types;
            qemu_log("got an empty signature for 0x%llx!\n", (uint64_t)invoke);
        }
        tcg_register_callback_as_type((target_ulong)invoke, signature);
    }
    BlockCopyFunction copy;
    BlockDisposeFunction dispose;
    block_get_copy_dispose(block, &copy, &dispose);
    if (copy) {
        tcg_register_callback_as_type((target_ulong)copy, block_copy_types);
    }
    if (dispose) {
        tcg_register_callback_as_type((target_ulong)dispose, block_dispose_types);
    }
    return;
}

// copy src block, and replace its callbacks with our x64 trampolines
struct Block_layout *
tcg_copy_replace_blocks(struct Block_layout *src) {
    if (NULL == src)
        return NULL;
    
    struct Block_layout *dst = block_copy_to_heap_or_not(src);
    
    BlockInvokeFunction invoke = block_get_invoke(dst);
    const char *signature = block_get_signature(dst);
    if (invoke) {
        if (NULL == signature) {
            signature = default_method_types;
            qemu_log("got an empty signature for 0x%llx!\n", (uint64_t)invoke);
        }
        block_set_invoke(dst, callback_query_and_add_trampoline((void *)invoke, signature));
    }
    BlockCopyFunction copy;
    BlockDisposeFunction dispose;
    block_get_copy_dispose(dst, &copy, &dispose);
    block_set_copy_dispose(dst,
                           callback_query_and_add_trampoline((void *)copy, block_copy_types),
                           callback_query_and_add_trampoline((void *)dispose, block_dispose_types));
    return dst;
}

void tcg_register_struct_with_fn_ptr(uint8_t *stptr, const char *fnName, int argIdx, uint32_t *offsetList) {
    if (NULL == stptr)
        return;
    
    uint32_t *offsetWithKind = offsetList;
    while (STFP_LIST_TERMINATOR != *offsetWithKind) {
        const uint32_t offset = (*offsetWithKind) & 0x0ffff;
        switch ((*offsetWithKind) >> 16) {
            case STFP_FUNPTR:
            {
                assert(offset <= 9999);
                char offset_cstr[5];
                assert(argIdx <= 62);
                char arg_cstr[3];
                char *fake_name = (char *)g_malloc0(1 + 4 + strlen(fnName) + 2 + 1);
                fake_name[0] = '1';
                
                sprintf(offset_cstr, "%d", offset);
                strcat(fake_name, offset_cstr);
                
                strcat(fake_name, fnName);
                
                sprintf(arg_cstr, "%d", argIdx);
                strcat(fake_name, arg_cstr);
                
                target_ulong *pval = (target_ulong *)(stptr + offset);
                tcg_register_callback_as_type(*pval, fake_name);
                // fake_name should not be freed, at least not here
                break;
            }
            case STFP_STRUCT:
            {
                uint8_t *newptr = stptr + offset;
                if (newptr != stptr && newptr) {
                    // FIXME: if there is a loop like stptr->newptr->stptr,
                    // then we will stuck here
                    tcg_register_struct_with_fn_ptr(newptr, fnName,
                                                    argIdx, offsetList);
                }
                break;
            }
            default:
                abort();
                break;
        }
        ++offsetWithKind;
    }
    // The list should not be freed. A function with STFP may be called multiple times.
}

static __unused
void tcg_register_block_byref(struct Block_byref *bb, int flags) {
    if (NULL == bb)
        return;
    flags = flags & BLOCK_ALL_COPY_DISPOSE_FLAGS;
    if (BLOCK_FIELD_IS_BLOCK == flags)
        return tcg_register_blocks((struct Block_layout *)bb);
    
    if ((BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK) != flags &&
        BLOCK_FIELD_IS_BYREF != flags)
        return;
    
    BlockByrefKeepFunction keep;
    BlockByrefDestroyFunction destroy;
    block_byref_get_keep_destroy(bb, &keep, &destroy);
    if (keep)
        tcg_register_callback_as_type((target_ulong)keep, block_keep_types);
    if (destroy)
        tcg_register_callback_as_type((target_ulong)destroy, block_destroy_types);
    struct Block_byref *oldFwd = bb;
    struct Block_byref *forwarding = block_byref_get_forwarding(oldFwd);
    while (forwarding != oldFwd && forwarding) {
        BlockByrefKeepFunction fwdKeep;
        BlockByrefDestroyFunction fwdDestroy;
        block_byref_get_keep_destroy(forwarding, &fwdKeep, &fwdDestroy);
        if (fwdKeep)
            tcg_register_callback_as_type((target_ulong)fwdKeep, block_keep_types);
        if (fwdDestroy)
            tcg_register_callback_as_type((target_ulong)fwdDestroy, block_destroy_types);
        oldFwd = forwarding;
        forwarding = block_byref_get_forwarding(oldFwd);
    }
    return;
}

// copy src block byref, and replace its callbacks with our x64 trampolines
static __unused struct Block_byref *
tcg_copy_replace_block_byref(struct Block_byref *src) {
    if (NULL == src)
        return NULL;
    
    struct Block_byref *dst = block_byref_copy_to_heap_or_not(src);
    
    BlockByrefKeepFunction keep;
    BlockByrefDestroyFunction destroy;
    block_byref_get_keep_destroy(dst, &keep, &destroy);
    block_byref_set_keep_destroy(dst,
                                 callback_query_and_add_trampoline((void *)keep, block_keep_types),
                                 callback_query_and_add_trampoline((void *)destroy, block_destroy_types));
    
    // forwarding keep and destroy are not replaced if BLOCK_BYREF_NEEDS_COPY
    struct Block_byref *oldFwd = dst;
    struct Block_byref *forwarding = block_byref_get_forwarding(oldFwd);
    while (forwarding != oldFwd && forwarding) {
        BlockByrefKeepFunction fwdKeep;
        BlockByrefDestroyFunction fwdDestroy;
        block_byref_get_keep_destroy(forwarding, &fwdKeep, &fwdDestroy);
#if BLOCK_BYREF_NEEDS_COPY
        if (fwdKeep)
            tcg_register_callback_as_type((target_ulong)fwdKeep, block_keep_types);
        if (fwdDestroy)
            tcg_register_callback_as_type((target_ulong)fwdDestroy, block_destroy_types);
#else
        block_byref_set_keep_destroy(forwarding,
                                     callback_query_and_add_trampoline((void *)fwdKeep, block_keep_types),
                                     callback_query_and_add_trampoline((void *)fwdDestroy, block_destroy_types));
#endif
        oldFwd = forwarding;
        forwarding = block_byref_get_forwarding(oldFwd);
    }
    return dst;
}

// AAPCS64 2020Q4
// 6.4.2 Parameter Passing Rules
// "Any part of a register or a stack slot that is not used for an argument
// (padding bits) has unspecified content at the callee entry point."
//
// AMD64 ABI Draft 0.99.7
// 3.2.3 Parameter Passing
// "When a value of type _Bool is returned or passed in a register or on the
// stack, bit 0 contains the truth value and bits 1 to 7 shall be zero. Other
// bits are left unspecified."
//
// Based on clang generated json, for x86_64, parameters with 8 or 16 bits
// size will be sign/zero extended by caller.

static void tcg_extend_arg_in_rax_by_loc_info(TCGContext *s, unsigned locInfo) {
    if (locInfo == TE_SExt8_32) {
        // movsbl %al, %eax
        tcg_out8(s, 0x0f); tcg_out8(s, 0xbe); tcg_out8(s, 0xc0);
    } else if (locInfo == TE_SExt16_32) {
        // movswl %ax, %eax
        tcg_out8(s, 0x0f); tcg_out8(s, 0xbf); tcg_out8(s, 0xc0);
    } else if (locInfo == TE_ZExt8_32) {
        // movzbl %al, %eax
        tcg_out8(s, 0x0f); tcg_out8(s, 0xb6); tcg_out8(s, 0xc0);
    } else if (locInfo == TE_ZExt16_32) {
        // movzwl %ax, %eax
        tcg_out8(s, 0x0f); tcg_out8(s, 0xb7); tcg_out8(s, 0xc0);
    }
}

static void tcg_extend_ret_in_rcx_by_reg_loc_info(TCGContext *s, unsigned x64Reg, unsigned locInfo) {
    if (x64Reg == TE_AL || x64Reg == TE_DL) {
        if (locInfo == TE_SExt8_32) {
            // movsbl %cl, %ecx
            tcg_out8(s, 0x0f); tcg_out8(s, 0xbe); tcg_out8(s, 0xc9);
        } else {
            assert(locInfo == TE_ZExt8_32 || locInfo == TE_Full);
            // movzbl %cl, %ecx
            tcg_out8(s, 0x0f); tcg_out8(s, 0xb6); tcg_out8(s, 0xc9);
        }
    } else if (x64Reg == TE_AX) {
        if (locInfo == TE_SExt16_32) {
            // movswl %cx, %ecx
            tcg_out8(s, 0x0f); tcg_out8(s, 0xbf); tcg_out8(s, 0xc9);
        } else {
            assert(locInfo == TE_ZExt16_32 || locInfo == TE_Full);
            // movzwl %cx, %ecx
            tcg_out8(s, 0x0f); tcg_out8(s, 0xb7); tcg_out8(s, 0xc9);
        }
    }
}

static
void abi_a2x_exit_translation(TCGContext *s, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, const char *str) {
    // note that now TCG_REG_RAX cannot be used as temporary register, nor TCG_REG_XMM0-1
    
    // map return value
    // x86_64 2 aarch64
    // direct 2 direct : direct in reg - direct in reg
    // direct 2 sret   : this is unlikely to happen
    // sret   2 direct : copy result from [sret reg] to aarch64 registers
    // sret   2 sret   : this is done in entry part
    ABIArgInfo xxRetInfo = xxInfo->rinfo, aaRetInfo = aaInfo->rinfo;
    unsigned xxRegCnt = te_num_of_reg(xxRetInfo), aaRegCnt = te_num_of_reg(aaRetInfo);
    if (xxInfo->isCxxStructor && 0 == xxRegCnt && 1 == aaRegCnt) {
        // CxxStructor in aarch64 need to return `this`
        // `this` pointer is passed in X0 (first argument) and returned in X0
        // nothing to do
        return;
    }
    if (xxInfo->isCxxStructor && 1 == xxRegCnt && 0 == aaRegCnt) {
        // CxxStructor in aarch64 does not need to return `this`
        // nothing to do
        return;
    }
    tcg_out_addi(s, TCG_REG_RSP, ALIGN2POW(xxInfo->bytes, 16));
    // now TCG_REG_RSP points to the sret space we reserved in entry part
    
    switch (te_is_indirect(xxRetInfo) * 2 + te_is_indirect(aaRetInfo)) {
        case 0b00: // similar to case 0b0000 when mapping arguments
        {
            if (xxRegCnt == aaRegCnt) {
                // map them one by one
                // FIXME: there is no S/Z extend info for return value in
                // prototype system. At present, do ZExt for those funcs.
                for (unsigned j = 0; j < xxRegCnt; ++j) {
                    if (TE_XMM0 <= te_get_reg(xxRetInfo, j) && te_get_reg(xxRetInfo, j) <= TE_XMM7) {
                        tcg_out_st128_aligned(s, TCG_TYPE_V128,
                                              x64_enum_2_reg(te_get_reg(xxRetInfo, j)),
                                              TCG_AREG0,
                                              a64_enum_2_offset(te_get_reg(aaRetInfo, j)));
                    } else {
                        unsigned locInfo = te_get_reg_locinfo(xxRetInfo, j);
                        unsigned x64Reg = te_get_reg(xxRetInfo, j);
                        tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RCX, x64_enum_2_reg(x64Reg));
                        tcg_extend_ret_in_rcx_by_reg_loc_info(s, x64Reg, locInfo);
                        tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RCX,
                                   TCG_AREG0, a64_enum_2_offset(te_get_reg(aaRetInfo, j)));
                    }
                }
            } else if (xxRegCnt < aaRegCnt) {
                // I think, it only happens when passing HFA arguments
                // some mappings are listed here:
                // {Sn, Sn+1, Sn+2, Sn+3} - {XMMm, XMMm+1}, higher 64 bits unused
                // {Sn, Sn+1, Sn+2}       - {XMMm, XMMm+1}
                // {Sn, Sn+1}             - {XMMm}
                if (1 == xxRegCnt && 2 == aaRegCnt) {
                    assert(TE_XMM0 <= te_get_reg(xxRetInfo, 0) && te_get_reg(xxRetInfo, 0) <= TE_XMM7);
                    assert(TE_S0 <= te_get_reg(aaRetInfo, 0) && te_get_reg(aaRetInfo, 0) <= TE_S6);
                    assert(te_get_reg(aaRetInfo, 0) + 1 == te_get_reg(aaRetInfo, 1));
                    // 1st S reg
                    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R8,
                                x64_enum_2_reg(te_get_reg(xxRetInfo, 0)));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_R8, TCG_AREG0,
                               a64_enum_2_offset(te_get_reg(aaRetInfo, 0)));
                    // 2nd S reg
                    tcg_out_shifti(s, SHIFT_SHR, TCG_REG_R8, 32);
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_R8, TCG_AREG0,
                               a64_enum_2_offset(te_get_reg(aaRetInfo, 1)));
                } else if (2 == xxRegCnt) {
                    assert(TE_XMM0 <= te_get_reg(xxRetInfo, 0) && te_get_reg(xxRetInfo, 0) <= TE_XMM6);
                    assert(te_get_reg(xxRetInfo, 0) + 1 == te_get_reg(xxRetInfo, 1));
                    assert(3 == aaRegCnt || 4 == aaRegCnt);
                    assert(TE_S0 <= te_get_reg(aaRetInfo, 0) && te_get_reg(aaRetInfo, 0) <= TE_S5);
                    assert(te_get_reg(aaRetInfo, 0) + 1 == te_get_reg(aaRetInfo, 1));
                    assert(te_get_reg(aaRetInfo, 1) + 1 == te_get_reg(aaRetInfo, 2));
                    // 1st S reg
                    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R8,
                                x64_enum_2_reg(te_get_reg(xxRetInfo, 0)));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_R8, TCG_AREG0,
                               a64_enum_2_offset(te_get_reg(aaRetInfo, 0)));
                    // 2nd S reg
                    tcg_out_shifti(s, SHIFT_SHR, TCG_REG_R8, 32);
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_R8, TCG_AREG0,
                               a64_enum_2_offset(te_get_reg(aaRetInfo, 1)));
                    // 3rd S reg, 2nd XMM
                    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R8,
                                x64_enum_2_reg(te_get_reg(xxRetInfo, 1)));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_R8, TCG_AREG0,
                               a64_enum_2_offset(te_get_reg(aaRetInfo, 2)));
                    if (4 == aaRegCnt) {
                        assert(te_get_reg(aaRetInfo, 0) <= TE_S4);
                        assert(te_get_reg(aaRetInfo, 2) + 1 == te_get_reg(aaRetInfo, 3));
                        // 4th S reg
                        tcg_out_shifti(s, SHIFT_SHR, TCG_REG_R8, 32);
                        tcg_out_st(s, TCG_TYPE_I32, TCG_REG_R8, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaRetInfo, 3)));
                    }
                } else {
                    assert(0);
                }
            } else {
                assert(0 && "xxRegCnt > aaRegCnt");
            }
            break;
        }
        case 0b01:
            printf("%s: direct 2 sret just happened!\n", str);
            break;
        case 0b10:
        {
            for (unsigned j = 1; j < aaRegCnt; ++j) {
                assert(te_get_reg(aaRetInfo, j - 1) + 1 == te_get_reg(aaRetInfo, j));
            }
            unsigned offset = 0;
            if (TE_D0 == te_get_reg(aaRetInfo, 0)) {
                for (unsigned j = 0; j < aaRegCnt; ++j) {
                    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R8, TCG_REG_RSP, offset);
                    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_R8, TCG_AREG0,
                               a64_enum_2_offset(te_get_reg(aaRetInfo, j)));
                    offset += 8;
                }
            } else if (TE_Q0 == te_get_reg(aaRetInfo, 0)) {
                for (unsigned j = 0; j < aaRegCnt; ++j) {
                    tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM8, TCG_REG_RSP, offset);
                    tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM8, TCG_AREG0,
                                          a64_enum_2_offset(te_get_reg(aaRetInfo, j)));
                    offset += 16;
                }
            } else {
                printf("aa reg %d\n", te_get_reg(aaRetInfo, 0));
                assert(0 && "possible");
            }
            tcg_out_addi(s, TCG_REG_RSP, BIG_ENOUGH_SRET_SIZE);
            break;
        }
        default:
            break;
    }
    
    // NOTE: make sure TCG_REG_RAX still holds the return value
    // return value is a callback function
    if (te_is_callback(xxInfo->rinfo)) {
        // normal type encodings will not start with "0" + original name + "-1" + "\0"
        char *fake_name = (char *)g_malloc0(1 + strlen(str) + 2 + 1);
        fake_name[0] = '0';
        // fake_name[1] = '\0';
        strcat(fake_name, str);
        strcat(fake_name, "-1");
        
        // prepare arguments
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_RAX);
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RSI, (tcg_target_ulong)fake_name);
        
        tcg_out_call(s, (tcg_insn_unit *)tcg_register_callback_as_type);
        // fake_name should not be freed, at least not here
    }
    // handle block
    if (te_is_block(xxInfo->rinfo)) {
        tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RDI, TCG_REG_RAX);
#if BLOCK_HAS_CALLBACK_TRAMPOLINE == 0
        tcg_out_call(s, (tcg_insn_unit *)tcg_register_blocks);
#else
        tcg_out_call(s, (tcg_insn_unit *)tcg_copy_replace_blocks);
        // replace. back to arm64
        tcg_out_st(s, TCG_TYPE_REG, TCG_REG_RAX, TCG_AREG0,
                   offsetof(CPUARMState, xregs[0]));
#endif
    }
    // handle structure with function pointer
    if (te_is_stfp(xxInfo->rinfo)) {
        tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RDI, TCG_REG_RAX);
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RSI, (tcg_target_ulong)str);
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RDX, -1);
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RCX,
                     (tcg_target_ulong)xxInfo->retStfpOffsetList);
        tcg_out_call(s, (tcg_insn_unit *)tcg_register_struct_with_fn_ptr);
    }
}

static
void abi_a2x_entry_translation(TCGContext *s, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, const char *str) {
    for (unsigned i = 0, e = xxInfo->nargs; i != e; ++i) {
        ABIArgInfo aaArgInfo = aaInfo->ainfo[i];
        char arg_num[3]; // assume nargs is less than 64
        // if this argument is a callback function, register it with a fake name
        if (te_is_callback(aaArgInfo)) {
            char *fake_name = (char *)g_malloc0(1 + strlen(str) + 2 + 1);
            fake_name[0] = '0';
            // fake_name[1] = '\0';
            strcat(fake_name, str);
            sprintf(arg_num, "%d", i);
            strcat(fake_name, arg_num);
            
            // replace original callback with callback trampoline
            if (te_is_mem(aaArgInfo)) {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R11, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                tcg_out_push(s, TCG_REG_R11);
                tcg_out_addi(s, TCG_REG_RSP, -8);
                
                unsigned offset = te_get_mem(aaArgInfo);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_R11, offset);
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RSI, (tcg_target_ulong)fake_name);
                tcg_out_call(s, (tcg_insn_unit *)callback_query_and_add_trampoline);
                
                tcg_out_addi(s, TCG_REG_RSP, 8);
                tcg_out_pop(s, TCG_REG_R11);
                
                tcg_out_st(s, TCG_TYPE_REG, TCG_REG_RAX, TCG_REG_R11, offset);
            } else {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RSI, (tcg_target_ulong)fake_name);
                tcg_out_call(s, (tcg_insn_unit *)callback_query_and_add_trampoline);
                tcg_out_st(s, TCG_TYPE_REG, TCG_REG_RAX, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
            }
            // fake_name should not be freed, at least not here
        }
        // handle block
        if (te_is_block(aaArgInfo)) {
#if BLOCK_HAS_CALLBACK_TRAMPOLINE == 0
            if (te_is_mem(aaArgInfo)) {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R11, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                unsigned offset = te_get_mem(aaArgInfo);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_R11, offset);
            } else {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
            }
            tcg_out_call(s, (tcg_insn_unit *)tcg_register_blocks);
            
#else
            if (te_is_mem(aaArgInfo)) {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R11, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                unsigned offset = te_get_mem(aaArgInfo);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_R11, offset);
            } else {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
            }
            
            tcg_out_call(s, (tcg_insn_unit *)tcg_copy_replace_blocks);
            
            // replace orig block with our copied block
            if (te_is_mem(aaArgInfo)) {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R11, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                unsigned offset = te_get_mem(aaArgInfo);
                tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_R11, offset);
            } else {
                tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
            }
#endif
        }
        // handle structure with function pointer
        if (te_is_stfp(aaArgInfo)) {
            if (te_is_mem(aaArgInfo)) {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R11, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                unsigned offset = te_get_mem(aaArgInfo);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_R11, offset);
            } else {
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
            }
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RSI, (tcg_target_ulong)str);
            tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RDX, i);
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RCX,
                         (tcg_target_ulong)xxInfo->argStfpOffsetLists[i]);
            tcg_out_call(s, (tcg_insn_unit *)tcg_register_struct_with_fn_ptr);
        }
    }
    
    // check return value first
    ABIArgInfo xxRetInfo = xxInfo->rinfo, aaRetInfo = aaInfo->rinfo;
    // x86_64 2 aarch64
    // direct 2 direct : handled in exit part
    // direct 2 sret   : this is unlikely to happen
    // sret   2 direct : copy result from [sret reg] to aarch64 registers,
    //                   handled in exit part. Reserve a space for sret.
    // sret   2 sret   : X8 -> sret reg
    switch (te_is_indirect(xxRetInfo) * 2 + te_is_indirect(aaRetInfo)) {
        case 0b01:
            printf("%s: direct 2 sret just happened!\n", str);
            break;
        case 0b10:
            tcg_out_addi(s, TCG_REG_RSP, -BIG_ENOUGH_SRET_SIZE);
            tcg_out_mov(s, TCG_TYPE_REG, x64_enum_2_reg(te_get_reg(xxRetInfo, 0)), TCG_REG_RSP);
            break;
        case 0b11:
            tcg_out_ld(s, TCG_TYPE_PTR, x64_enum_2_reg(te_get_reg(xxRetInfo, 0)),
                       TCG_AREG0, offsetof(CPUARMState, xregs[8]));
            break;
        default:
            break;
    }
    tcg_out_addi(s, TCG_REG_RSP, -ALIGN2POW(xxInfo->bytes, 16));
    // map arguments
    for (unsigned i = 0, e = xxInfo->nargs; i != e; ++i) {
        ABIArgInfo xxArgInfo = xxInfo->ainfo[i], aaArgInfo = aaInfo->ainfo[i];
        // how many registers are used by this argument
        unsigned xxRegCnt = te_num_of_reg(xxArgInfo), aaRegCnt = te_num_of_reg(aaArgInfo);
        // the argument may be of kind "Ignore"
        if ((!te_is_mem(xxArgInfo) && 0 == xxRegCnt) || (!te_is_mem(aaArgInfo) && 0 == aaRegCnt))
            continue;
        
        switch ((te_is_indirect(xxArgInfo) << 3) +
                (te_is_mem(xxArgInfo)    << 2) +
                (te_is_indirect(aaArgInfo) << 1) +
                (te_is_mem(aaArgInfo))) {
            case 0b0000: // direct in reg - direct in reg
            {
                if (xxRegCnt == aaRegCnt) {
                    // map them one by one
                    for (unsigned j = 0; j < xxRegCnt; ++j) {
                        if (TE_XMM0 <= te_get_reg(xxArgInfo, j) && te_get_reg(xxArgInfo, j) <= TE_XMM7) {
                            tcg_out_ld128_aligned(s, TCG_TYPE_V128,
                                                  x64_enum_2_reg(te_get_reg(xxArgInfo, j)),
                                                  TCG_AREG0,
                                                  a64_enum_2_offset(te_get_reg(aaArgInfo, j)));
                        } else {
                            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX,
                                       TCG_AREG0, a64_enum_2_offset(te_get_reg(aaArgInfo, j)));
                            unsigned locInfo = te_get_reg_locinfo(xxArgInfo, j);
                            unsigned x64Reg = te_get_reg(xxArgInfo, j);
                            tcg_extend_arg_in_rax_by_loc_info(s, locInfo);
                            tcg_out_mov(s, TCG_TYPE_REG, x64_enum_2_reg(x64Reg), TCG_REG_RAX);
                        }
                    }
                } else if (xxRegCnt < aaRegCnt) {
                    // I think, it only happens when passing HFA arguments
                    // some mappings are listed here:
                    // {Sn, Sn+1, Sn+2, Sn+3} - {XMMm, XMMm+1}, I think higher 64 bits are unused
                    // {Sn, Sn+1, Sn+2}       - {XMMm, XMMm+1}
                    // {Sn, Sn+1}             - {XMMm}
                    // Distinguish them by their counts.
                    if (1 == xxRegCnt && 2 == aaRegCnt) {
                        assert(TE_XMM0 <= te_get_reg(xxArgInfo, 0) && te_get_reg(xxArgInfo, 0) <= TE_XMM7);
                        assert(te_get_reg(aaArgInfo, 0) + 1 == te_get_reg(aaArgInfo, 1));
                        // concat 2 S regs
                        tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, 1)));
                        tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, 0)) + 4);
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128,
                                              x64_enum_2_reg(te_get_reg(xxArgInfo, 0)),
                                              TCG_AREG0,
                                              a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
                    } else if (2 == xxRegCnt) {
                        assert(TE_XMM0 <= te_get_reg(xxArgInfo, 0) && te_get_reg(xxArgInfo, 0) <= TE_XMM6);
                        assert(te_get_reg(xxArgInfo, 0) + 1 == te_get_reg(xxArgInfo, 1));
                        assert(3 == aaRegCnt || 4 == aaRegCnt);
                        assert(te_get_reg(aaArgInfo, 0) + 1 == te_get_reg(aaArgInfo, 1));
                        assert(te_get_reg(aaArgInfo, 1) + 1 == te_get_reg(aaArgInfo, 2));
                        // concat first 2 S regs
                        tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, 1)));
                        tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, 0)) + 4);
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128,
                                              x64_enum_2_reg(te_get_reg(xxArgInfo, 0)),
                                              TCG_AREG0,
                                              a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
                        // 3rd and 4th S reg
                        tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, 3)));
                        tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, 2)) + 4);
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128,
                                              x64_enum_2_reg(te_get_reg(xxArgInfo, 1)),
                                              TCG_AREG0,
                                              a64_enum_2_offset(te_get_reg(aaArgInfo, 2)));
                    } else {
                        assert(0);
                    }
                } else {
                    assert(0 && "xxRegCnt > aaRegCnt");
                }
                break;
            }
            case 0b0001: // direct in reg - direct in mem
            {
                unsigned offset_base = te_get_mem(aaArgInfo);
                unsigned offset = 0;
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R11, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                for (unsigned j = 0; j < xxRegCnt; ++j) {
                    if (TE_XMM0 <= te_get_reg(xxArgInfo, j) && te_get_reg(xxArgInfo, j) <= TE_XMM7) {
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128, x64_enum_2_reg(te_get_reg(xxArgInfo, j)),
                                              TCG_REG_R11, offset_base + offset);
                        offset += te_get_arg_size(aaArgInfo) / xxRegCnt;
                    } else {
                        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX,
                                   TCG_REG_R11, offset_base + offset);
                        unsigned locInfo = te_get_reg_locinfo(xxArgInfo, j);
                        unsigned x64Reg = te_get_reg(xxArgInfo, j);
                        tcg_extend_arg_in_rax_by_loc_info(s, locInfo);
                        tcg_out_mov(s, TCG_TYPE_REG, x64_enum_2_reg(x64Reg), TCG_REG_RAX);
                        offset += 8;
                    }
                }
                break;
            }
            case 0b0010: // direct in reg - indirect in reg
            case 0b0011: // direct in reg - indirect in mem
                assert(0 && "impossible");
                break;
            case 0b0100: // direct in mem - direct in reg
            {
                // some mappings are listed here
                // [mem] - {Q0, Q1}
                // [mem] - {Q0, Q1, Q2}
                // [mem] - {Q0, Q2, Q3, Q4}
                // [mem] - {D0, D1, D2, D3}
                // [mem] - {X6}
                unsigned offset_base = te_get_mem(xxArgInfo);
                unsigned offset = 0;
                if (aaRegCnt * 16 == te_get_arg_size(xxArgInfo)) {
                    for (unsigned j = 0; j < aaRegCnt; ++j) {
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM8, TCG_AREG0,
                                              a64_enum_2_offset(te_get_reg(aaArgInfo, j)));
                        tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM8, TCG_REG_RSP,
                                              offset_base + offset);
                        offset += 16;
                    }
                } else {
                    for (unsigned j = 0; j < aaRegCnt; ++j) {
                        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0,
                                   a64_enum_2_offset(te_get_reg(aaArgInfo, j)));
                        unsigned locInfo = te_get_mem_locinfo(xxArgInfo);
                        tcg_extend_arg_in_rax_by_loc_info(s, locInfo);
                        tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSP,
                                   offset_base + offset);
                        offset += 8;
                    }
                }
                break;
            }
            case 0b0101: // direct in mem - direct in mem
            {
                unsigned aa_offset = te_get_mem(aaArgInfo);
                unsigned xx_offset = te_get_mem(xxArgInfo);
                const unsigned arg_size = te_get_arg_size(aaArgInfo);
                assert(arg_size == te_get_arg_size(xxArgInfo));
                
                tcg_out_push(s, TCG_REG_RDI);
                tcg_out_push(s, TCG_REG_RSI);
                tcg_out_push(s, TCG_REG_RCX);
                
                tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RCX, arg_size);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RSI, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                tcg_out_addi(s, TCG_REG_RSI, aa_offset);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_RSP);
                tcg_out_addi(s, TCG_REG_RDI, xx_offset + 8 * 3);
                tcg_out8(s, 0xf3); // rep
                tcg_out8(s, 0xa4); // movsb
                
                tcg_out_pop(s, TCG_REG_RCX);
                tcg_out_pop(s, TCG_REG_RSI);
                tcg_out_pop(s, TCG_REG_RDI);
                
                if (arg_size <= 2) {
                    unsigned locInfo = te_get_mem_locinfo(xxArgInfo);
                    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSP, xx_offset);
                    tcg_extend_arg_in_rax_by_loc_info(s, locInfo);
                    tcg_out_st(s, TCG_TYPE_REG, TCG_REG_RAX, TCG_REG_RSP, xx_offset);
                }
                break;
            }
            case 0b0110: // direct in mem - indirect in reg
            {
                unsigned xx_offset = te_get_mem(xxArgInfo);
                unsigned arg_size = te_get_arg_size(xxArgInfo);
                
                tcg_out_push(s, TCG_REG_RDI);
                tcg_out_push(s, TCG_REG_RSI);
                tcg_out_push(s, TCG_REG_RCX);
                
                tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RCX, arg_size);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RSI, TCG_AREG0,
                           a64_enum_2_offset(te_get_reg(aaArgInfo, 0)));
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_RSP);
                tcg_out_addi(s, TCG_REG_RDI, xx_offset + 8 * 3);
                tcg_out8(s, 0xf3); // rep
                tcg_out8(s, 0xa4); // movsb
                
                tcg_out_pop(s, TCG_REG_RCX);
                tcg_out_pop(s, TCG_REG_RSI);
                tcg_out_pop(s, TCG_REG_RDI);
                
                assert(arg_size > 2);
                break;
            }
            case 0b0111: // direct in mem - indirect in mem
            {
                unsigned aa_offset = te_get_mem(aaArgInfo);
                unsigned xx_offset = te_get_mem(xxArgInfo);
                unsigned arg_size = te_get_arg_size(xxArgInfo);
                
                tcg_out_push(s, TCG_REG_RDI);
                tcg_out_push(s, TCG_REG_RSI);
                tcg_out_push(s, TCG_REG_RCX);
                
                tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RCX, arg_size);
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0,
                           offsetof(CPUARMState, xregs[REG_SP_NUM]));
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RSI, TCG_REG_RAX, aa_offset);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_RSP);
                tcg_out_addi(s, TCG_REG_RDI, xx_offset + 8 * 3);
                tcg_out8(s, 0xf3); // rep
                tcg_out8(s, 0xa4); // movsb
                
                tcg_out_pop(s, TCG_REG_RCX);
                tcg_out_pop(s, TCG_REG_RSI);
                tcg_out_pop(s, TCG_REG_RDI);
                
                assert(arg_size > 2);
                break;
            }
            default:
                assert(0 && "only arguments in aarch64 may be passed indirectly");
                break;
        }
    }
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RAX, xxInfo->ssecount);
}

static GHashTable *str_a2x_codesize = NULL;

static GHashTable *name_a2x_codeptr = NULL;
void set_name_as_a2x_codeptr(const char *name, void *stub, size_t size) {
    // lock is already held
    if (NULL == name_a2x_codeptr) {
        name_a2x_codeptr = g_hash_table_new(NULL, NULL);
    }
    if (NULL == str_a2x_codesize) {
        str_a2x_codesize = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(name_a2x_codeptr, (gpointer)name, (gpointer)stub);
    g_hash_table_insert(str_a2x_codesize, (gpointer)name, (gpointer)size);
}

void *get_a2x_codeptr_by_name(const char *name, size_t *size) {
    if (NULL == name_a2x_codeptr)
        return NULL;
    if (size)
        *size = (size_t)g_hash_table_lookup(str_a2x_codesize, (gconstpointer)name);
    return g_hash_table_lookup(name_a2x_codeptr, (gconstpointer)name);
}

static GHashTable *types_a2x_codeptr = NULL;
void set_types_as_a2x_codeptr(const char *types, void *stub, size_t size) {
    if (NULL == types_a2x_codeptr) {
        types_a2x_codeptr = g_hash_table_new(NULL, NULL);
    }
    if (NULL == str_a2x_codesize) {
        str_a2x_codesize = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(types_a2x_codeptr, (gpointer)types, (gpointer)stub);
    g_hash_table_insert(str_a2x_codesize, (gpointer)types, (gpointer)size);
}

void *get_a2x_codeptr_by_types(const char *types, size_t *size) {
    if (NULL == types_a2x_codeptr)
        return NULL;
    if (size)
        *size = (size_t)g_hash_table_lookup(str_a2x_codesize, (gconstpointer)types);
    return g_hash_table_lookup(types_a2x_codeptr, (gconstpointer)types);
}

extern void print_func_name(const char *);

static void gen_print_func_name(TCGContext *s, const char *name) {
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RDI, (tcg_target_ulong)name);
    tcg_out_call(s, (tcg_insn_unit *)print_func_name);
}

void *
abi_a2x_gen_trampoline_for_name(const char *symbolname, size_t *genSize)
{
    TCGContext *s = tcg_ctx;
    
    // printf("*debug* fname: %s\n", symbolname);
    
    // common process for name
    void *stub_code = NULL;
    
    stub_code = get_a2x_codeptr_by_name(symbolname, genSize);
    if (stub_code)
        return stub_code;
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_fn_info_from_name(symbolname, &aaInfo, &xxInfo)) {
        printf("something went wrong, %s\n", symbolname);
        abort();
    }
    
    code_gen_start(s);
    stub_code = s->code_ptr;
    
    save_lr_to_stack(s);
    
#if DEBUG == 1
    gen_print_func_name(s, symbolname);
#endif
    
    // handle entry
    abi_a2x_entry_translation(s, &aaInfo, &xxInfo, symbolname);
    
    // If the symbolname is a fake name of callback, then
    // pc is not invariant. So we obtain it at runtime.
    tcg_out_ld(s, TCG_TYPE_REG, TCG_REG_R11, TCG_AREG0, offsetof(CPUARMState, pc));
    {
        tcg_out8(s, 0x41); tcg_out8(s, 0xff); tcg_out8(s, 0xd3);
    } // call *%r11
    
    // handle exit
    abi_a2x_exit_translation(s, &aaInfo, &xxInfo, symbolname);
    
    update_pc_from_lr(s);
    // general tb retn
    tcg_out_jmp(s, s->code_gen_quick_tbcache);
    
    code_gen_finalize(s);
    
#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(LOG_ABI_BRIDGE)) {
        iqemu_bridge_asm_log("a2x", symbolname, stub_code, s->code_ptr);
    }
#endif
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    // the offset list of each argument/return should not be freed
    g_free(xxInfo.argStfpOffsetLists);
    
    size_t code_gen_size = (void *)s->code_ptr - stub_code;
    set_name_as_a2x_codeptr(symbolname, stub_code, code_gen_size);
    
    if (genSize)
        *genSize = code_gen_size;
    
    return stub_code;
}

void *
abi_a2x_gen_trampoline_for_types(const char *types, size_t *genSize)
{
    TCGContext *s = tcg_ctx;

    // common process for type encodings
    void *stub_code = NULL;
    
    stub_code = get_a2x_codeptr_by_types(types, genSize);
    if (stub_code)
        return stub_code;
    
    ABIFnInfo aaInfo, xxInfo;
    if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
        printf("something went wrong, %s\n", types);
        abort();
    }
    
    code_gen_start(s);
    stub_code = s->code_ptr;
    
    save_lr_to_stack(s);
    
    // handle entry
    abi_a2x_entry_translation(s, &aaInfo, &xxInfo, types);
    
    // pc is not invariant. So we obtain it at runtime.
    tcg_out_ld(s, TCG_TYPE_REG, TCG_REG_R11, TCG_AREG0, offsetof(CPUARMState, pc));
    {
        tcg_out8(s, 0x41); tcg_out8(s, 0xff); tcg_out8(s, 0xd3);
    } // call *%r11
    
    // handle exit
    abi_a2x_exit_translation(s, &aaInfo, &xxInfo, types);
    
    update_pc_from_lr(s);
    // general tb retn
    tcg_out_jmp(s, s->code_gen_quick_tbcache);
    
    code_gen_finalize(s);
    
#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(LOG_ABI_BRIDGE)) {
        iqemu_bridge_asm_log("a2x", types, stub_code, s->code_ptr);
    }
#endif
    
    g_free(aaInfo.ainfo);
    g_free(xxInfo.ainfo);
    
    size_t code_gen_size = (void *)s->code_ptr - stub_code;
    set_types_as_a2x_codeptr(types, stub_code, code_gen_size);

    if (genSize)
        *genSize = code_gen_size;
    
    return stub_code;
}

#undef save_lr_to_stack
#undef update_pc_from_lr

#undef register_args
#undef BIG_ENOUGH_SRET_SIZE
#undef REG_SP_NUM
