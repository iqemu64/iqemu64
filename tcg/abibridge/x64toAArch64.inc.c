/*
 * Function:    abi_x2a_get_translation_function_pair
 * Description: It returns 2 JIT functions that translate
 *              parameters and return values accordingly
 *              according to the pc.
 */

typedef struct objc_method {
    const char *method_name;
    const char *method_types;
    void *method_imp;
} *Method;

static
void abi_x2a_get_translation_function_pair_by_types_nolock(const char *types,
                                                    fnEntryTranslation *entry_translation,
                                                    fnExitTranslation *exit_translation);
/*
 * mmap_lock is held in calling this function.
 */
uint8_t *
tcg_gen_objc_trampoline(TCGContext *s, Method method, uint32_t mark)
{
    fnEntryTranslation entry_translation = NULL;
    fnExitTranslation exit_translation = NULL;
    
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
    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_R10, TCG_AREG0, offsetof(CPUARMState, pc));
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R10, (target_ulong)entry_translation);
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_R11, (target_ulong)exit_translation);
    tcg_out_jmp(s, (tcg_insn_unit *)s->code_gen_xloop_trampoline);
    
    code_gen_finalize(s);
    return begin;
}

static const char *tcg_query_callback_type(target_ulong pc);

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
    //
    // TODO: This is just a crappy example
    
    if(!strcmp(types, "i16i0[*]8")) {
        //
        // main() uses this.
        static fnEntryTranslation s_main_entry = NULL;
        static fnExitTranslation s_main_exit = NULL;
        if(NULL == s_main_entry) {
            code_gen_start(s);
            s_main_entry = (void *)s->code_ptr;
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rdi));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[0]));
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rsi));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[1]));
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            
            code_gen_finalize(s);
        }
        if(NULL == s_main_exit) {
            code_gen_start(s);
            s_main_exit = (void *)s->code_ptr;
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[0]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rax));
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            
            code_gen_finalize(s);
        }
        *entry_translation = s_main_entry;
        *exit_translation = s_main_exit;
    } else if(!strcmp(types, "@36@0:8^S16Q24B32")) {
        //
        // initWithCharactersNoCopy uses this.
        static fnEntryTranslation s_entry = NULL;
        static fnExitTranslation s_exit = NULL;
        if(NULL == s_entry) {
            code_gen_start(s);
            s_entry = (void *)s->code_ptr;
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rdi));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[0]));
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rsi));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[1]));
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rdx));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[2]));
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rcx));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[3]));
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, r8));
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[4]));
            
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            
            code_gen_finalize(s);
        }
        if(NULL == s_exit) {
            code_gen_start(s);
            s_exit = (void *)s->code_ptr;
            
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RDI, offsetof(CPUARMState, xregs[0]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RAX, TCG_REG_RSI, offsetof(struct _Register_Context, rax));
            tcg_out_opc(s, OPC_RET, 0, 0, 0);   // ret
            
            code_gen_finalize(s);
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

const char *tcg_query_callback_type(target_ulong pc)
{
    // TODO: use an efficient way here, and remove locks.
    const char *r;
    
    mmap_lock();
    r = g_hash_table_lookup(callback_types, (gconstpointer)pc);
    mmap_unlock();
    return r;
}
