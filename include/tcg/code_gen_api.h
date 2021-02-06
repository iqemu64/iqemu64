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

#ifndef code_gen_api_h
#define code_gen_api_h

static inline
void code_gen_start(TCGContext *s)
{
    tcg_func_start(s);
    s->code_ptr = s->code_gen_ptr;
    s->code_buf = s->code_gen_ptr;
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
