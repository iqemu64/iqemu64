
//
// TODO: This is a very crappy example
// IMPORTANT: Both functions are presumed mmap_lock held!

static void *gen_1param_1ret_trampolines(TCGContext *s, target_ulong pc)
{
    code_gen_start(s);
    void *stub = s->code_ptr;
    
    //
    // 1 ptr parameter for this function.
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
    tcg_out_call(s, (tcg_insn_unit *)pc);
    //
    // void * return value.
    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
    //
    // update pc from lr.
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30]));
    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, pc));
    // general tb retn
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, 0);
    tcg_out_jmp(s, (tcg_insn_unit *)tb_ret_addr);
    
    code_gen_finalize(s);
    
    return stub;
}

void *
abi_a2x_gen_trampoline_for_name(const char *symbolname, target_ulong pc)
{
    TCGContext *s = tcg_ctx;
    
    if(!strcmp(symbolname, "objc_autoreleasePoolPush")) {
        static void *stub_objc_autoreleasePoolPush = NULL;
        if(NULL == stub_objc_autoreleasePoolPush) {
            code_gen_start(s);
            stub_objc_autoreleasePoolPush = s->code_ptr;
            
            //
            // No parameters for this function.
            tcg_out_call(s, (tcg_insn_unit *)pc);
            //
            // void * return value.
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
            //
            // update pc from lr.
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30]));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, pc));
            // general tb retn
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, 0);
            tcg_out_jmp(s, (tcg_insn_unit *)tb_ret_addr);
            
            code_gen_finalize(s);
        }
        return stub_objc_autoreleasePoolPush;
    } else if(!strcmp(symbolname, "objc_alloc")) {
        static void *stub = NULL;
        if(NULL == stub) {
            stub = gen_1param_1ret_trampolines(s, pc);
        }
        return stub;
    } else if(!strcmp(symbolname, "objc_retainAutoreleasedReturnValue")) {
        static void *stub = NULL;
        if(NULL == stub) {
            stub = gen_1param_1ret_trampolines(s, pc);
        }
        return stub;
    } else if(!strcmp(symbolname, "objc_retainAutorelease")) {
        static void *stub = NULL;
        if(NULL == stub) {
            stub = gen_1param_1ret_trampolines(s, pc);
        }
        return stub;
    } else if(!strcmp(symbolname, "objc_exception_throw")) {     // This one does not have
                                                                // return value, but that's
                                                                // all right.
        static void *stub = NULL;
        if(NULL == stub) {
            stub = gen_1param_1ret_trampolines(s, pc);
        }
        return stub;
    } else if(!strcmp(symbolname, "objc_begin_catch")) {
        static void *stub = NULL;
        if(NULL == stub) {
            stub = gen_1param_1ret_trampolines(s, pc);
        }
        return stub;
    } else if(!strcmp(symbolname, "objc_retain")) {
        static void *stub = NULL;
        if(NULL == stub) {
            stub = gen_1param_1ret_trampolines(s, pc);
        }
        return stub;
    } else if(!strcmp(symbolname, "objc_exception_rethrow")) {
        static void *stub = NULL;
        if(NULL == stub) {
            code_gen_start(s);
            stub = s->code_ptr;
            
            //
            // No parameters for this function.
            tcg_out_call(s, (tcg_insn_unit *)pc);
            
            //
            // And no return value.
            // update pc from lr.
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30]));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, pc));
            // general tb retn
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, 0);
            tcg_out_jmp(s, (tcg_insn_unit *)tb_ret_addr);
            
            code_gen_finalize(s);
        }
        return stub;
    } else {
        abort();
    }
    return NULL;
}

void *
abi_a2x_gen_trampoline_for_types(const char *types, target_ulong pc)
{
    TCGContext *s = tcg_ctx;
    
    if(!strcmp(types, "@24@0:8@16")) {
        static void *stub_code = NULL;
        if(NULL == stub_code) {
            code_gen_start(s);
            
            stub_code = s->code_ptr;
            
            //
            // 3 ptr parameters for this function.
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RSI, TCG_AREG0, offsetof(CPUARMState, xregs[1]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDX, TCG_AREG0, offsetof(CPUARMState, xregs[2]));
            tcg_out_call(s, (tcg_insn_unit *)pc);
            //
            // void * return value.
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
            //
            // update pc from lr.
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30]));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, pc));
            // general tb retn
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, 0);
            tcg_out_jmp(s, (tcg_insn_unit *)tb_ret_addr);
            
            code_gen_finalize(s);
        }
        return stub_code;
    } else if(!strcmp(types, "@40@0:8@16@24@32")) {
        static void *stub_code = NULL;
        if(NULL == stub_code) {
            code_gen_start(s);
            
            stub_code = s->code_ptr;
            
            //
            // 5 ptr parameters for this function.
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDI, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RSI, TCG_AREG0, offsetof(CPUARMState, xregs[1]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RDX, TCG_AREG0, offsetof(CPUARMState, xregs[2]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RCX, TCG_AREG0, offsetof(CPUARMState, xregs[3]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R8, TCG_AREG0, offsetof(CPUARMState, xregs[4]));
            tcg_out_call(s, (tcg_insn_unit *)pc);
            //
            // void * return value.
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[0]));
            //
            // update pc from lr.
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, xregs[30]));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_AREG0, offsetof(CPUARMState, pc));
            // general tb retn
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, 0);
            tcg_out_jmp(s, (tcg_insn_unit *)tb_ret_addr);
            
            code_gen_finalize(s);
        }
        return stub_code;
    }
    return NULL;
}
