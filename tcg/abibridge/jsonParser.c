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

#include <stdio.h>
#include "mpack/mpack.h"
#include "tcg/ABIArgInfo.h"
#include "type-encoding/aa_reg.h"
#include "type-encoding/xx_reg.h"
#include "glib.h"


static inline
void parse_file(const char *filepath, mpack_tree_t *tree) {
    mpack_tree_init_filename(tree, filepath, 0);
    mpack_tree_parse(tree);
}

static inline
uint64_t next_uint64_value(mpack_node_data_t **fwd) {
    uint64_t r = (*fwd)->value.u;
    *fwd = *fwd + 1;
    return r;
}

static inline
unsigned next_uint_value(mpack_node_data_t **fwd) {
    return (unsigned)next_uint64_value(fwd);
}

static inline
bool reach_end(mpack_node_data_t *fwd, mpack_node_t array) {
    size_t len = mpack_node_array_length(array);
    return (fwd == array.data->value.children + len);
}

static mpack_tree_t mpTree;
static mpack_node_t mpRoot;

#pragma mark - Interface

void init_prototype_system() {
    const gsize pathLen = 210;
    GString *path = g_string_sized_new(pathLen);
    g_string_append(path, getenv("DYLD_ROOT_PATH"));
    const char *relative = "/usr/local/lib/all-functions.msgpack";
    g_string_append(path, relative);
    
    parse_file(path->str, &mpTree);
    mpRoot = mpack_tree_root(&mpTree);
    
    g_string_free(path, true);
}

bool get_fn_info_from_name(const char *name, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo) {
    memset(xxInfo, 0, sizeof(ABIFnInfo));
    memset(aaInfo, 0, sizeof(ABIFnInfo));
    
    mpack_node_t fnInfoObj = mpack_node_map_cstr(mpRoot, name);
    if (mpack_node_is_nil(fnInfoObj)) {
        return false;
    }
    
    // currently, each json obj has the following format
    // "fnName": [arm64.rinfo.val, size, arm64.rinfo.mask,
    //            x86_64.rinfo.val, size, x86_64.rinfo.mask,
    //            nargs, nargs * ainfo in two archs,
    //            arm64.bytes, x86_64.bytes,
    //            x86_64.ssecount, isCxxStructor, variadic,
    //            non-zero-stfp-size and stfp-list from ret to last arg]
    mpack_node_data_t *arrayFwd = fnInfoObj.data->value.children;
    
    // rinfo
    aaInfo->rinfo.val = next_uint64_value(&arrayFwd);
    aaInfo->rinfo.size = next_uint_value(&arrayFwd);
    aaInfo->rinfo.mask = next_uint_value(&arrayFwd);
    
    xxInfo->rinfo.val = next_uint64_value(&arrayFwd);
    xxInfo->rinfo.size = next_uint_value(&arrayFwd);
    xxInfo->rinfo.mask = next_uint_value(&arrayFwd);
    
    // nargs
    aaInfo->nargs = xxInfo->nargs = next_uint_value(&arrayFwd);
    
    // ainfo
    const unsigned malloc_size = aaInfo->nargs * sizeof(ABIArgInfo);
    aaInfo->ainfo = (ABIArgInfo *)g_malloc0(malloc_size);
    xxInfo->ainfo = (ABIArgInfo *)g_malloc0(malloc_size);
    for (unsigned i = 0, e = aaInfo->nargs; i != e; ++i) {
        aaInfo->ainfo[i].val = next_uint64_value(&arrayFwd);
        aaInfo->ainfo[i].size = next_uint_value(&arrayFwd);
        aaInfo->ainfo[i].mask = next_uint_value(&arrayFwd);
    }
    for (unsigned i = 0, e = aaInfo->nargs; i != e; ++i) {
        xxInfo->ainfo[i].val = next_uint64_value(&arrayFwd);
        xxInfo->ainfo[i].size = next_uint_value(&arrayFwd);
        xxInfo->ainfo[i].mask = next_uint_value(&arrayFwd);
    }
    
    // bytes
    aaInfo->bytes = next_uint_value(&arrayFwd);
    xxInfo->bytes = next_uint_value(&arrayFwd);
    
    // ssecount
    xxInfo->ssecount = next_uint_value(&arrayFwd);
    
    // isCxxStructor
    aaInfo->isCxxStructor = xxInfo->isCxxStructor = next_uint_value(&arrayFwd);
    
    // variadic
    aaInfo->variadic = xxInfo->variadic = next_uint_value(&arrayFwd);
    if (aaInfo->variadic) {
        return false;
    }
    
    // struct with fn ptr
    // Note: the retStfpList and argStfpLists do not depend on architecture,
    // so we only initialize for xxInfo.
    if (te_is_stfp(xxInfo->rinfo)) {
        const unsigned rslen = next_uint_value(&arrayFwd);
        xxInfo->retStfpOffsetList = (uint32_t *)g_malloc0((1 + rslen) *
                                                          sizeof(uint32_t));
        xxInfo->retStfpOffsetList[rslen] = STFP_LIST_TERMINATOR;
        for (unsigned i = 0; i != rslen; ++i) {
            xxInfo->retStfpOffsetList[i] = next_uint_value(&arrayFwd);
        }
    }
    
    if (reach_end(arrayFwd, fnInfoObj))
        return true;
    
    xxInfo->argStfpOffsetLists = (uint32_t **)g_malloc0(xxInfo->nargs *
                                                        sizeof(uint32_t *));
    for (unsigned arg_idx = 0, e = xxInfo->nargs; arg_idx != e; ++arg_idx) {
        if (te_is_stfp(xxInfo->ainfo[arg_idx])) {
            const unsigned aslen = next_uint_value(&arrayFwd);
            xxInfo->argStfpOffsetLists[arg_idx] =
                (uint32_t *)g_malloc0((1 + aslen) * sizeof(uint32_t));
            xxInfo->argStfpOffsetLists[arg_idx][aslen] = STFP_LIST_TERMINATOR;
            for (unsigned i = 0; i != aslen; ++i) {
                xxInfo->argStfpOffsetLists[arg_idx][i] = next_uint_value(&arrayFwd);
            }
        }
    }
    
    assert(reach_end(arrayFwd, fnInfoObj));
    return true;
}
