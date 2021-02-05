#ifndef code_gen_api_h
#define code_gen_api_h

static inline
void code_gen_start(TCGContext *s)
{
    tcg_func_start(s);
    s->code_ptr = s->code_gen_ptr;
    
}

static inline
void code_gen_finalize(TCGContext *s)
{
    if (tcg_out_pool_finalize(s) < 0) {
        abort();
    }
    
    if (!tcg_resolve_relocs(s)) {
        abort();
    }
    
    atomic_set(&tcg_ctx->code_gen_ptr, (void *)
               ROUND_UP((uintptr_t)s->code_ptr, CODE_GEN_ALIGN));
}

#endif /* code_gen_api_h */
