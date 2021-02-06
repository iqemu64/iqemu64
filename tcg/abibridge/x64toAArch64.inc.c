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

/*
 * Function:    abi_x2a_get_translation_function_pair
 * Description: It returns 2 JIT functions that translate
 *              parameters and return values accordingly
 *              according to the pc.
 */

#include "type-encoding/aa_reg.h"
#include "type-encoding/xx_reg.h"
#include "tcg/ABIArgInfo.h"

#define CACHE_NAME_X2A 1
#define CACHE_TYPES_X2A 1

#define REG_SP_NUM 31


/*
 * mmap_lock is held in calling this function.
 */
uint8_t *
tcg_gen_common_trampoline(TCGContext *s, void *orig_entry, const char *types)
{
    fnEntryTranslation entry_translation = NULL;
    fnExitTranslation exit_translation = NULL;
    
    abi_x2a_get_translation_function_pair_by_types_nolock(types, &entry_translation, &exit_translation);
    
    code_gen_start(s);
    
    uint8_t *begin = s->code_ptr;
    
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R10, (tcg_target_ulong)orig_entry);
    tcg_out_push(s, TCG_REG_R10);
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R10, (target_ulong)entry_translation);
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R11, (target_ulong)exit_translation);
    tcg_out_jmp(s, (tcg_insn_unit *)s->code_gen_xloop_trampoline);
    
    code_gen_finalize(s);
    
    return begin;
}
/*
* mmap_lock is held in calling this function.
*/
uint8_t *
tcg_gen_objc_impBlock_trampoline(TCGContext *s, Method method, uint32_t mark)
{
    code_gen_start(s);
    
    uint8_t *begin = s->code_ptr;
    tcg_out8(s, OPC_JMP_short);
    tcg_out8(s, sizeof(uint32_t) + sizeof(void *) + sizeof(struct objc_method));     /* how many bytes we want to skip */
    tcg_out32(s, mark);
    tcg_out64(s, (uint64_t)method); /* pointer to the 'real' method_t */
    
    tcg_out64(s, (uint64_t)method->method_name);   /* the fake Method, change the imp to 'begin' */
    tcg_out64(s, (uint64_t)method->method_types);
    tcg_out64(s, (uint64_t)begin);
    tcg_out_jmp(s, (tcg_insn_unit *)method->method_imp);
    
    code_gen_finalize(s);
    return begin;
}

/*
 * mmap_lock is held in calling this function.
 */
uint8_t *
tcg_gen_objc_trampoline(TCGContext *s, Method method, uint32_t mark)
{
    fnEntryTranslation entry_translation = NULL;
    fnExitTranslation exit_translation = NULL;
    
    if (!strstr(method->method_types, "std::"))
        abi_x2a_get_translation_function_pair_by_types_nolock(method->method_types, &entry_translation, &exit_translation);
    
    code_gen_start(s);
    
    uint8_t *begin = s->code_ptr;
        
    tcg_out8(s, OPC_JMP_short);
    tcg_out8(s, sizeof(uint32_t) + sizeof(void *) + sizeof(struct objc_method));     /* how many bytes we want to skip */
    tcg_out32(s, mark);
    tcg_out64(s, (uint64_t)method); /* pointer to the 'real' method_t */
    
    tcg_out64(s, (uint64_t)method->method_name);   /* the fake Method, change the imp to 'begin' */
    tcg_out64(s, (uint64_t)method->method_types);
    tcg_out64(s, (uint64_t)begin);
    
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R10, (tcg_target_ulong)method->method_imp);
    tcg_out_push(s, TCG_REG_R10);
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R10, (target_ulong)entry_translation);
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R11, (target_ulong)exit_translation);
    tcg_out_jmp(s, (tcg_insn_unit *)s->code_gen_xloop_trampoline);
    
    code_gen_finalize(s);
    return begin;
}

const char *tcg_query_callback_type(target_ulong pc);

void abi_x2a_get_translation_function_pair_by_pc(target_ulong pc,
                                                 fnEntryTranslation *entry_translation,
                                                 fnExitTranslation *exit_translation)
{
    const char *types = tcg_query_callback_type(pc);
    mmap_lock();
    abi_x2a_get_translation_function_pair_by_types_nolock(types, entry_translation, exit_translation);
    mmap_unlock();
}

void abi_x2a_get_translation_function_pari_by_types(const char *types,
                                                    fnEntryTranslation *entry_translation,
                                                    fnExitTranslation *exit_translation)
{
    mmap_lock();
    abi_x2a_get_translation_function_pair_by_types_nolock(types, entry_translation, exit_translation);
    mmap_unlock();
}

extern
bool get_fn_info_from_types(const char *str, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

extern
bool get_fn_info_from_name(const char *str, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

// keep this code synchronized with enum x86_64_reg and _Register_Context
static inline
size_t x64_enum_2_offset(int num) {
    switch (num) {
        case TE_RAX:
        case TE_EAX:
        case TE_AX:
        case TE_AL:
            return offsetof(struct _Register_Context, rax);
        case TE_RDI:
        case TE_EDI:
            return offsetof(struct _Register_Context, rdi);
        case TE_RSI:
        case TE_ESI:
            return offsetof(struct _Register_Context, rsi);
        case TE_RDX:
        case TE_EDX:
        case TE_DL:
            return offsetof(struct _Register_Context, rdx);
        case TE_RCX:
        case TE_ECX:
            return offsetof(struct _Register_Context, rcx);
        case TE_R8:
        case TE_R8D:
            return offsetof(struct _Register_Context, r8);
        case TE_R9:
        case TE_R9D:
            return offsetof(struct _Register_Context, r9);
        default:
            return offsetof(struct _Register_Context, xmm0) + 16 * (num - TE_XMM0);
    }
    return 0;
}

#define ld_x86_64(tcg_reg, reg)  \
tcg_out_ld(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RSI, offsetof(struct _Register_Context, reg))

#define st_x86_64(tcg_reg, reg)  \
tcg_out_st(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RSI, offsetof(struct _Register_Context, reg))

#define ld_x86_64_enum(tcg_reg, num) \
tcg_out_ld(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RSI, x64_enum_2_offset(num))

#define st_x86_64_enum(tcg_reg, num) \
tcg_out_st(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RSI, x64_enum_2_offset(num))

#define ld_aarch64(tcg_reg, reg) \
tcg_out_ld(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RDI, offsetof(CPUARMState, reg))

#define st_aarch64(tcg_reg, reg) \
tcg_out_st(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RDI, offsetof(CPUARMState, reg))

#define ld_aarch64_enum(tcg_reg, num)    \
tcg_out_ld(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RDI, a64_enum_2_offset(num))

#define st_aarch64_enum(tcg_reg, num)    \
tcg_out_st(s, TCG_TYPE_PTR, tcg_reg, TCG_REG_RDI, a64_enum_2_offset(num))

static
void abi_x2a_entry_translation(TCGContext *s, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, const char *str) {
    for (unsigned i = 0, e = xxInfo->nargs; i != e; ++i) {
        ABIArgInfo xxArgInfo = xxInfo->ainfo[i];
        if (te_is_block(xxArgInfo)) {
            // save rdi, rsi
            tcg_out_push(s, TCG_REG_RDI);
            tcg_out_push(s, TCG_REG_RSI);
            
            if (te_is_mem(xxArgInfo)) {
                ld_x86_64(TCG_REG_R10, rsp);
                unsigned offset = te_get_mem(xxArgInfo);
                // rsp points to ret addr
                tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_R10, offset + 8);
            } else {
                ld_x86_64_enum(TCG_REG_R10, te_get_reg(xxArgInfo, 0));
                tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_RDI, TCG_REG_R10);
            }
#if (BLOCK_HAS_CALLBACK_TRAMPOLINE == 0) || BLOCK_NEEDS_STACK_COPY
            tcg_out_addi(s, TCG_REG_RSP, -8);
            tcg_out_call(s, (tcg_insn_unit *)tcg_register_blocks);
            tcg_out_addi(s, TCG_REG_RSP, 8);
            // restore rdi, rsi
            tcg_out_pop(s, TCG_REG_RSI);
            tcg_out_pop(s, TCG_REG_RDI);
#else
            tcg_out_addi(s, TCG_REG_RSP, -8);
            tcg_out_call(s, (tcg_insn_unit *)tcg_copy_replace_blocks);
            tcg_out_addi(s, TCG_REG_RSP, 8);
            
            // restore rdi, rsi
            tcg_out_pop(s, TCG_REG_RSI);
            tcg_out_pop(s, TCG_REG_RDI);
            
            // replace orig block with our copied block
            // NOTE: this has to be after rdi and rsi are restored
            if (te_is_mem(xxArgInfo)) {
                ld_x86_64(TCG_REG_R10, rsp);
                unsigned offset = te_get_mem(xxArgInfo);
                // rsp points to  ret addr
                tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_R10, offset + 8);
            } else {
                st_x86_64_enum(TCG_REG_RAX, te_get_reg(xxArgInfo, 0));
            }
#endif
        }
    }
    
    // check return value first
    ABIArgInfo xxRetInfo = xxInfo->rinfo, aaRetInfo = aaInfo->rinfo;
    // x86_64 2 aarch64
    // direct 2 direct : handled in exit part
    // direct 2 sret   : this is unlikely to happen
    // sret   2 direct : copy result from aarch64 registers to [sret reg],
    //                   handled in exit part
    // sret   2 sret   : sret reg -> X8
    switch (te_is_indirect(xxRetInfo) * 2 + te_is_indirect(aaRetInfo)) {
        case 0b01:
            printf("%s: direct 2 sret just happened!\n", str);
            break;
        case 0b11:
            ld_x86_64_enum(TCG_REG_RAX, te_get_reg(xxRetInfo, 0));
            st_aarch64(TCG_REG_RAX, xregs[8]);
            break;
        default:
            break;
    }
    
    const TCGReg aarch64_sp_holder = TCG_REG_R10;
    const TCGReg x86_64_rsp_holder = TCG_REG_R11;
    {
        ld_aarch64(TCG_REG_RAX, xregs[REG_SP_NUM]);
        tcg_out_addi(s, TCG_REG_RAX, -ALIGN2POW(aaInfo->bytes, 16));
        st_aarch64(TCG_REG_RAX, xregs[REG_SP_NUM]);
        // ld_aarch64(aarch64_sp_holder, xregs[REG_SP_NUM]);
        tcg_out_mov(s, TCG_TYPE_PTR, aarch64_sp_holder, TCG_REG_RAX);
    } // hold aarch64 sp
    {
        ld_x86_64(x86_64_rsp_holder, rsp); // rsp points to ret addr on stack
        tcg_out_addi(s, x86_64_rsp_holder, 8);
    } // hold x86_64 rsp
    
    // map arguments
    for (unsigned i = 0, e = xxInfo->nargs; i != e; ++i) {
        ABIArgInfo xxArgInfo = xxInfo->ainfo[i], aaArgInfo = aaInfo->ainfo[i];
        // how many registers are used by this argument
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
                    // not sign/zero extended
                    for (unsigned j = 0; j < xxRegCnt; ++j) {
                        if (TE_XMM0 <= te_get_reg(xxArgInfo, j) && te_get_reg(xxArgInfo, j) <= TE_XMM7) {
                            tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RSI,
                                                  x64_enum_2_offset(te_get_reg(xxArgInfo, j)));
                            tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RDI,
                                                  a64_enum_2_offset(te_get_reg(aaArgInfo, j)));
                        } else {
                            ld_x86_64_enum(TCG_REG_RAX, te_get_reg(xxArgInfo, j));
                            st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, j));
                        }
                    }
                } else if (xxRegCnt < aaRegCnt) {
                    // I think, it only happens when passing HFA arguments
                    // some mappings are listed here:
                    // {Sn, Sn+1, Sn+2, Sn+3} - {XMMm, XMMm+1}, higher 64 bits unused
                    // {Sn, Sn+1, Sn+2}       - {XMMm, XMMm+1}
                    // {Sn, Sn+1}             - {XMMm}
                    // Distinguish them by their counts.
                    if (1 == xxRegCnt && 2 == aaRegCnt) {
                        assert(TE_XMM0 <= te_get_reg(xxArgInfo, 0) && te_get_reg(xxArgInfo, 0) <= TE_XMM7);
                        assert(te_get_reg(aaArgInfo, 0) + 1 == te_get_reg(aaArgInfo, 1));
                        // 1st S reg
                        ld_x86_64_enum(TCG_REG_RAX, te_get_reg(xxArgInfo, 0));
                        st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 0));
                        // 2nd S reg
                        tcg_out_shifti(s, SHIFT_SHR, TCG_REG_RAX, 32);
                        st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 1));
                    } else if (2 == xxRegCnt) {
                        assert(TE_XMM0 <= te_get_reg(xxArgInfo, 0) && te_get_reg(xxArgInfo, 0) <= TE_XMM6);
                        assert(te_get_reg(xxArgInfo, 0) + 1 == te_get_reg(xxArgInfo, 1));
                        assert(3 == aaRegCnt || 4 == aaRegCnt);
                        assert(te_get_reg(aaArgInfo, 0) + 1 == te_get_reg(aaArgInfo, 1));
                        assert(te_get_reg(aaArgInfo, 1) + 1 == te_get_reg(aaArgInfo, 2));
                        // 1st S reg
                        ld_x86_64_enum(TCG_REG_RAX, te_get_reg(xxArgInfo, 0));
                        st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 0));
                        // 2nd S reg
                        tcg_out_shifti(s, SHIFT_SHR, TCG_REG_RAX, 32);
                        st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 1));
                        // 3rd S reg, 2nd XMM
                        ld_x86_64_enum(TCG_REG_RAX, te_get_reg(xxArgInfo, 1));
                        st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 2));
                        if (4 == aaRegCnt) {
                            assert(te_get_reg(aaArgInfo, 2) + 1 == te_get_reg(aaArgInfo, 3));
                            // 4th S reg
                            tcg_out_shifti(s, SHIFT_SHR, TCG_REG_RAX, 32);
                            st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 3));
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
                unsigned offset_base = te_get_mem(aaArgInfo);
                unsigned offset = 0;
                for (unsigned j = 0; j < xxRegCnt; ++j) {
                    if (TE_XMM0 <= te_get_reg(xxArgInfo, j) && te_get_reg(xxArgInfo, j) <= TE_XMM7) {
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RSI,
                                              x64_enum_2_offset(te_get_reg(xxArgInfo, j)));
                        tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, aarch64_sp_holder,
                                              offset_base + offset);
                        offset += te_get_arg_size(aaArgInfo) / xxRegCnt;
                    } else {
                        ld_x86_64_enum(TCG_REG_RAX, te_get_reg(xxArgInfo, j));
                        tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, aarch64_sp_holder,
                                   offset_base + offset);
                        // Only variadic functions will fall into this case.
                        // According to iOS ARM64 ABI, each argument is 8-byte aligned.
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
                unsigned offset_base = te_get_mem(xxArgInfo);
                unsigned offset = 0;
                if (aaRegCnt * 16 == te_get_arg_size(xxArgInfo)) {
                    for (unsigned j = 0; j < aaRegCnt; ++j) {
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, x86_64_rsp_holder,
                                              offset_base + offset);
                        tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RDI,
                                              a64_enum_2_offset(te_get_reg(aaArgInfo, j)));
                        offset += 16;
                    }
                } else {
                    for (unsigned j = 0; j < aaRegCnt; ++j) {
                        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, x86_64_rsp_holder,
                                   offset_base + offset);
                        st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, j));
                        offset += 8;
                    }
                }
                break;
            }
            case 0b0101: // direct in mem - direct in mem
            {
                unsigned aa_offset = te_get_mem(aaArgInfo);
                unsigned xx_offset = te_get_mem(xxArgInfo);
                const unsigned arg_size = te_get_arg_size(xxArgInfo);
                assert(arg_size == te_get_arg_size(aaArgInfo));
                // save rdi, rsi
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R8, TCG_REG_RDI);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R9, TCG_REG_RSI);
                
                tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_RCX, arg_size);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RSI, x86_64_rsp_holder);
                tcg_out_addi(s, TCG_REG_RSI, xx_offset);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RDI, aarch64_sp_holder);
                tcg_out_addi(s, TCG_REG_RDI, aa_offset);
                tcg_out8(s, 0xf3); // rep
                tcg_out8(s, 0xa4); // movsb
                
                // restore rdi, rsi
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RSI, TCG_REG_R9);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_REG_R8);
                break;
            }
            case 0b0110: // direct in mem - indirect in reg
            {
                unsigned xx_offset = te_get_mem(xxArgInfo);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RAX, x86_64_rsp_holder);
                tcg_out_addi(s, TCG_REG_RAX, xx_offset);
                st_aarch64_enum(TCG_REG_RAX, te_get_reg(aaArgInfo, 0));
                break;
            }
            case 0b0111: // direct in mem - indirect in mem
            {
                unsigned xx_offset = te_get_mem(xxArgInfo);
                unsigned aa_offset = te_get_mem(aaArgInfo);
                tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_RAX, x86_64_rsp_holder);
                tcg_out_addi(s, TCG_REG_RAX, xx_offset);
                tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, aarch64_sp_holder, aa_offset);
                break;
            }
            default:
                assert(0 && "only arguments in aarch64 may be passed indirectly");
                break;
        }
    }
    
}

static
void abi_x2a_exit_translation(TCGContext *s, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo, const char *str) {
    // map return value
    // x86_64 2 aarch64
    // direct 2 direct : direct in reg - direct in reg
    // direct 2 sret   : this is unlikely to happen
    // sret   2 direct : copy result from aarch64 registers to [RDI]
    // sret   2 sret   : this is done in entry part
    ABIArgInfo xxRetInfo = xxInfo->rinfo, aaRetInfo = aaInfo->rinfo;
    unsigned xxRegCnt = te_num_of_reg(xxRetInfo), aaRegCnt = te_num_of_reg(aaRetInfo);
    
    if (xxInfo->isCxxStructor && 0 == xxRegCnt && 1 == aaRegCnt) {
        // CxxStructor in x86_64 does not need to return `this`
        // nothing to do
        return;
    }
    if (xxInfo->isCxxStructor && 1 == xxRegCnt && 0 == aaRegCnt) {
        // CxxStructor in x86_64 need to return `this`
        // `this` pointer is passed in RDI (first argument) and returned in RAX
        ld_x86_64(TCG_REG_RAX, rdi);
        st_x86_64(TCG_REG_RAX, rax);
        return;
    }
    
    switch (te_is_indirect(xxRetInfo) * 2 + te_is_indirect(aaRetInfo)) {
        case 0b00: // similar to case 0b0000 when mapping arguments
        {
            if (xxRegCnt == aaRegCnt) {
                // map them one by one
                // not sign/zero extended
                for (unsigned j = 0; j < xxRegCnt; ++j) {
                    if (TE_XMM0 <= te_get_reg(xxRetInfo, j) && te_get_reg(xxRetInfo, j) <= TE_XMM7) {
                        tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RDI,
                                              a64_enum_2_offset(te_get_reg(aaRetInfo, j)));
                        tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RSI,
                                              x64_enum_2_offset(te_get_reg(xxRetInfo, j)));
                    } else {
                        ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, j));
                        st_x86_64_enum(TCG_REG_RAX, te_get_reg(xxRetInfo, j));
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
                    ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, 0));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_REG_RSI,
                               x64_enum_2_offset(te_get_reg(xxRetInfo, 0)));
                    // 2nd S reg
                    ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, 1));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_REG_RSI,
                               x64_enum_2_offset(te_get_reg(xxRetInfo, 0)) + 4);
                } else if (2 == xxRegCnt) {
                    assert(TE_XMM0 <= te_get_reg(xxRetInfo, 0) && te_get_reg(xxRetInfo, 0) <= TE_XMM6);
                    assert(te_get_reg(xxRetInfo, 0) + 1 == te_get_reg(xxRetInfo, 1));
                    assert(3 == aaRegCnt || 4 == aaRegCnt);
                    assert(TE_S0 <= te_get_reg(aaRetInfo, 0) && te_get_reg(aaRetInfo, 0) <= TE_S5);
                    assert(te_get_reg(aaRetInfo, 0) + 1 == te_get_reg(aaRetInfo, 1));
                    assert(te_get_reg(aaRetInfo, 1) + 1 == te_get_reg(aaRetInfo, 2));
                    // 1st S reg
                    ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, 0));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_REG_RSI,
                               x64_enum_2_offset(te_get_reg(xxRetInfo, 0)));
                    // 2nd S reg
                    ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, 1));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_REG_RSI,
                               x64_enum_2_offset(te_get_reg(xxRetInfo, 0)) + 4);
                    // 3rd S reg, 2nd XMM
                    ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, 2));
                    tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_REG_RSI,
                               x64_enum_2_offset(te_get_reg(xxRetInfo, 1)));
                    if (4 == aaRegCnt) {
                        assert(te_get_reg(aaRetInfo, 0) <= TE_S4);
                        assert(te_get_reg(aaRetInfo, 2) + 1 == te_get_reg(aaRetInfo, 3));
                        // 4th S reg
                        ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, 3));
                        tcg_out_st(s, TCG_TYPE_I32, TCG_REG_RAX, TCG_REG_RSI,
                                   x64_enum_2_offset(te_get_reg(xxRetInfo, 1)) + 4);
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
            ld_x86_64_enum(TCG_REG_R8, te_get_reg(xxRetInfo, 0));
            if (TE_D0 == te_get_reg(aaRetInfo, 0)) {
                for (unsigned j = 0; j < aaRegCnt; ++j) {
                    ld_aarch64_enum(TCG_REG_RAX, te_get_reg(aaRetInfo, j));
                    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_R8, offset);
                    offset += 8;
                }
            } else if (TE_Q0 == te_get_reg(aaRetInfo, 0)) {
                for (unsigned j = 0; j < aaRegCnt; ++j) {
                    tcg_out_ld128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_RDI,
                                          a64_enum_2_offset(te_get_reg(aaRetInfo, j)));
                    tcg_out_st128_aligned(s, TCG_TYPE_V128, TCG_REG_XMM0, TCG_REG_R8, offset);
                    offset += 16;
                }
            } else {
                printf("aa reg %d\n", te_get_reg(aaRetInfo, 0));
                assert(0 && "possible");
            }
            break;
        }
        default:
            break;
    }

    ld_aarch64(TCG_REG_RAX, xregs[REG_SP_NUM]);
    tcg_out_addi(s, TCG_REG_RAX, ALIGN2POW(aaInfo->bytes, 16));
    st_aarch64(TCG_REG_RAX, xregs[REG_SP_NUM]);
    
    if (te_is_block(xxInfo->rinfo)) {
        // save rdi, rsi
        tcg_out_push(s, TCG_REG_RDI);
        tcg_out_push(s, TCG_REG_RSI);
        
        ld_aarch64(TCG_REG_RDI, xregs[0]);
#if BLOCK_HAS_CALLBACK_TRAMPOLINE == 0
        tcg_out_addi(s, TCG_REG_RSP, -8);
        tcg_out_call(s, (tcg_insn_unit *)tcg_register_blocks);
        tcg_out_addi(s, TCG_REG_RSP, 8);
#else
        tcg_out_addi(s, TCG_REG_RSP, -8);
        tcg_out_call(s, (tcg_insn_unit *)tcg_copy_replace_blocks);
        tcg_out_addi(s, TCG_REG_RSP, 8);
        // replace. back to x86_64
        st_x86_64(TCG_REG_RAX, rax);
#endif
        // restore rdi, rsi
        tcg_out_pop(s, TCG_REG_RSI);
        tcg_out_pop(s, TCG_REG_RDI);
    }
    
}

#undef ld_x86_64

#undef st_x86_64

#undef ld_x86_64_enum

#undef st_x86_64_enum

#undef ld_aarch64

#undef st_aarch64

#undef ld_aarch64_enum

#undef st_aarch64_enum

static GHashTable *name_x2a_entry_codeptr = NULL;
void set_name_as_x2a_entry_codeptr(const char *name, void *stub) {
    // lock is already held
    if (NULL == name_x2a_entry_codeptr) {
        name_x2a_entry_codeptr = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(name_x2a_entry_codeptr, (gpointer)name, (gpointer)stub);
}

void *get_x2a_entry_codeptr_by_name(const char *name) {
    // lock is already held
    if (NULL == name_x2a_entry_codeptr)
        return NULL;
    return g_hash_table_lookup(name_x2a_entry_codeptr, (gconstpointer)name);
}

static GHashTable *name_x2a_exit_codeptr = NULL;
void set_name_as_x2a_exit_codeptr(const char *name, void *stub) {
    // lock is already held
    if (NULL == name_x2a_exit_codeptr) {
        name_x2a_exit_codeptr = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(name_x2a_exit_codeptr, (gpointer)name, (gpointer)stub);
}

void *get_x2a_exit_codeptr_by_name(const char *name) {
    // lock is already held
    if (NULL == name_x2a_exit_codeptr)
        return NULL;
    return g_hash_table_lookup(name_x2a_exit_codeptr, (gconstpointer)name);
}

static GHashTable *types_x2a_entry_codeptr = NULL;
void set_types_as_x2a_entry_codeptr(const char *types, void *stub) {
    // lock is already held
    if (NULL == types_x2a_entry_codeptr) {
        types_x2a_entry_codeptr = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(types_x2a_entry_codeptr, (gpointer)types, (gpointer)stub);
}

void *get_x2a_entry_codeptr_by_types(const char *types) {
    // lock is already held
    if (NULL == types_x2a_entry_codeptr)
        return NULL;
    return g_hash_table_lookup(types_x2a_entry_codeptr, (gconstpointer)types);
}

static GHashTable *types_x2a_exit_codeptr = NULL;
void set_types_as_x2a_exit_codeptr(const char *types, void *stub) {
    // lock is already held
    if (NULL == types_x2a_exit_codeptr) {
        types_x2a_exit_codeptr = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(types_x2a_exit_codeptr, (gpointer)types, (gpointer)stub);
}

void *get_x2a_exit_codeptr_by_types(const char *types) {
    // lock is already held
    if (NULL == types_x2a_exit_codeptr)
        return NULL;
    return g_hash_table_lookup(types_x2a_exit_codeptr, (gconstpointer)types);
}

/*
 * mmap_lock is held in calling this function
 */
static
void abi_x2a_get_translation_function_pair_by_types_nolock(const char *types,
                                                    fnEntryTranslation *entry_translation,
                                                    fnExitTranslation *exit_translation)
{
    *entry_translation = NULL;
    *exit_translation = NULL;
    if(!types) return;
    
    TCGContext *s = tcg_ctx;
    if ('0' == types[0] || '1' == types[0]) {
        // handle callback functions and struct with function pointer
        // currently they have fake names start with '0' or '1'
        fnEntryTranslation s_entry = NULL;
        fnExitTranslation s_exit = NULL;
        
#if CACHE_NAME_X2A
        s_entry = get_x2a_entry_codeptr_by_name(types);
        s_exit = get_x2a_exit_codeptr_by_name(types);
#endif
        if (NULL == s_entry) {
            assert(NULL == s_exit);
            ABIFnInfo aaInfo, xxInfo;
            if (!get_fn_info_from_name(types, &aaInfo, &xxInfo)) {
                printf("something went wrong, %s\n", types);
                abort();
            }
            
            // handle entry
            code_gen_start(s);
            s_entry = (void *)s->code_ptr;
            
            abi_x2a_entry_translation(s, &aaInfo, &xxInfo, types);
            
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            code_gen_finalize(s);
            
#ifdef DEBUG_DISAS
            if (qemu_loglevel_mask(LOG_ABI_BRIDGE)) {
                iqemu_bridge_asm_log("x2a entry", types, s_entry, s->code_ptr);
            }
#endif
            
            // handle exit
            code_gen_start(s);
            s_exit = (void *)s->code_ptr;
            
            abi_x2a_exit_translation(s, &aaInfo, &xxInfo, types);
            
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            code_gen_finalize(s);
            
#ifdef DEBUG_DISAS
            if (qemu_loglevel_mask(LOG_ABI_BRIDGE)) {
                iqemu_bridge_asm_log("x2a exit", types, s_exit, s->code_ptr);
            }
#endif
            
            g_free(aaInfo.ainfo);
            g_free(xxInfo.ainfo);
            
#if CACHE_NAME_X2A
            set_name_as_x2a_entry_codeptr(types, s_entry);
            set_name_as_x2a_exit_codeptr(types, s_exit);
#endif
        }
        
        *entry_translation = s_entry;
        *exit_translation = s_exit;
    } else {
        // common process of type string
        fnEntryTranslation s_entry = NULL;
        fnExitTranslation s_exit = NULL;
        
#if CACHE_TYPES_X2A
        s_entry = get_x2a_entry_codeptr_by_types(types);
        s_exit = get_x2a_exit_codeptr_by_types(types);
#endif
        if (NULL == s_entry) {
            assert(NULL == s_exit);
            ABIFnInfo aaInfo, xxInfo;
            if (!get_fn_info_from_types(types, &aaInfo, &xxInfo)) {
                printf("something went wrong, %s\n", types);
                abort();
            }
            
            // handle entry
            code_gen_start(s);
            s_entry = (void *)s->code_ptr;
            
            abi_x2a_entry_translation(s, &aaInfo, &xxInfo, types);
            
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            code_gen_finalize(s);
            
#ifdef DEBUG_DISAS
            if (qemu_loglevel_mask(LOG_ABI_BRIDGE)) {
                iqemu_bridge_asm_log("x2a entry", types, s_entry, s->code_ptr);
            }
#endif
            
            // handle exit
            code_gen_start(s);
            s_exit = (void *)s->code_ptr;
            
            abi_x2a_exit_translation(s, &aaInfo, &xxInfo, types);
            
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            code_gen_finalize(s);
            
#ifdef DEBUG_DISAS
            if (qemu_loglevel_mask(LOG_ABI_BRIDGE)) {
                iqemu_bridge_asm_log("x2a exit", types, s_exit, s->code_ptr);
            }
#endif
            
            g_free(aaInfo.ainfo);
            g_free(xxInfo.ainfo);
            
#if CACHE_TYPES_X2A
            set_types_as_x2a_entry_codeptr(types, s_entry);
            set_types_as_x2a_exit_codeptr(types, s_exit);
#endif
        }
        
        *entry_translation = s_entry;
        *exit_translation = s_exit;
    }
}

static GHashTable *callback_types = NULL;
void tcg_register_callback_as_type(target_ulong pc, const char *types)
{
    // TODO: use an efficient way here, and remove locks.
    mmap_lock();
    if(NULL == callback_types) {
        callback_types = g_hash_table_new(NULL, NULL);
    }
    
    g_hash_table_insert(callback_types, (gpointer)pc, (gpointer)types);
    mmap_unlock();
}

void tcg_register_callback_as_type_nolock(target_ulong pc, const char *types)
{
    if (NULL == callback_types) {
        callback_types = g_hash_table_new(NULL, NULL);
    }
    
    g_hash_table_insert(callback_types, (gpointer)pc, (gpointer)types);
}

const char *tcg_query_callback_type(target_ulong pc)
{
    // TODO: use an efficient way here, and remove locks.
    const char *r;
    
    mmap_lock();
    r = g_hash_table_lookup(callback_types, (gconstpointer)pc);
    mmap_unlock();
    return r;
}

const char *tcg_query_callback_type_nolock(target_ulong pc)
{
    return g_hash_table_lookup(callback_types, (gconstpointer)pc);
}

#undef REG_SP_NUM

#undef CACHE_TYPES_X2A
#undef CACHE_NAME_X2A
