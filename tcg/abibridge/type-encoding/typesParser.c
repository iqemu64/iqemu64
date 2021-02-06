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

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "te_ffi.h"
#include "glib.h"

#ifndef NDEBUG
#define DEBUG_THIS_FILE 1
#endif

#if DEBUG_THIS_FILE
#include <stdio.h>
#define dbgf(format, args...) printf(format, args)
#else
#define dbgf(format, args...)
#endif

const unsigned InvalidValue = ~0U;
const char *smplType = "cislqCISLQfdBv*#:?";
const char *aggrType = "{}[]()";
const char *aggrOpen = "{[(";
const char *qualifiers = "rnNoORV";

static
void *mycalloc(size_t num, size_t size, GArray *ptr_to_free) {
    void *p = g_malloc0(num * size);
    assert(p && "out of memory");
    g_array_append_val(ptr_to_free, p);
    return p;
}

static
void moveFwdAggregate(const char **str, char openBrace) {
    const char *pos = strchr(aggrType, openBrace);
    char closeBrace = *(pos + 1);
    // begin, str points to ---!---
    // for example, in v24@0:8[3{_st=[2*]}]16
    // end, str points to -----------------^-
    uint8_t left = 1;
    while (left != 0) {
        if (**str == openBrace)
            ++left;
        else if (**str == closeBrace)
            --left;
        *str = *str + 1;
    }
}

// sync with fn getType
static
void moveFwdAType(const char **str) {
    if (strchr(smplType, **str)) {
        *str = *str + 1;
    } else {
        const char *pos = strchr(aggrOpen, **str);
        if (pos != NULL) {
            *str = *str + 1;
            moveFwdAggregate(str, *pos);
        } else if (**str == '^') {
            *str = *str + 1;
            moveFwdAType(str);
        } else if (strchr(qualifiers, **str)) {
            *str = *str + 1;
            moveFwdAType(str);
        } else if (**str == '@') {
            *str = *str + 1;
            if (**str == '"') {
                *str = *str + 1;
                while (**str != '"')
                    *str = *str + 1;
                *str = *str + 1;
            } else if (**str == '?') {
                *str = *str + 1;
                if (**str == '<') {
                    while (**str != '>')
                        *str = *str + 1;
                    *str = *str + 1;
                }
            }
        } else {
            dbgf("encounter invalid char: %s\n", *str - 1);
            abort();
        }
    }
}

static
ffi_type *getType(const char **str, bool arrAsAggr, bool *isBlockType,
                  GArray *ptr_to_free);

static
ffi_type *getArrayType(const char **str, bool arrAsAggr, GArray *ptr_to_free) {
    if (!arrAsAggr) {
        moveFwdAggregate(str, '[');
        // `void f(int a[])` equals `void f(int *a)`
        return &ffi_type_pointer;
    }

    const unsigned nelmt = (unsigned)strtol(*str, (char **)str, 10);
    ffi_type *elmt_ty = getType(str, true, NULL, ptr_to_free);
    // move str right after ']'
    assert(**str == ']');
    *str = *str + 1;

    ffi_type **arr_elmts = (ffi_type **)mycalloc(nelmt + 1, sizeof(ffi_type *),
                                                 ptr_to_free);
    ffi_type **fwd = arr_elmts, **end = arr_elmts + nelmt; // NULL terminated
    while (fwd != end)
        *(fwd++) = elmt_ty;

    ffi_type *arr_type = (ffi_type *)mycalloc(1, sizeof(ffi_type), ptr_to_free);
    // arr_type->size = arr_type->alignment = 0;
    arr_type->type = FFI_TYPE_STRUCT;
    arr_type->elements = arr_elmts;
    return arr_type;
}

// build a new (instance of) ffi_type at runtime
// caller should release the resources
// http://www.chiark.greenend.org.uk/doc/libffi-dev/html/Type-Example.html
static
ffi_type *getStructType(const char **str, GArray *ptr_to_free) {
    // move str right after '='
    while (**str != '=')
        *str = *str + 1;
    *str = *str + 1;
    
    GArray *elmts_vec = g_array_sized_new(true, false, sizeof(ffi_type *), 4);
    while (**str != '}') {
        // arrayType in a struct is DIFFERENT from that in a function
        ffi_type *ty = getType(str, true, NULL, ptr_to_free);
        g_array_append_val(elmts_vec, ty);
    }
    // move str right after '}'
    *str = *str + 1;
    
    ffi_type **st_elmts = (ffi_type **)g_array_free(elmts_vec, false);
    g_array_append_val(ptr_to_free, st_elmts);

    ffi_type *st_type = (ffi_type *)mycalloc(1, sizeof(ffi_type), ptr_to_free);
    // st_type->size = st_type->alignment = 0;
    st_type->type = FFI_TYPE_STRUCT;
    st_type->elements = st_elmts;
    return st_type;
}

static
ffi_type *getUnionType(const char **str, GArray *ptr_to_free) {
    // libffi has no special support for unions or bit-fields
    // https://github.com/libffi/libffi/blob/v3.3/doc/libffi.texi#L537-L578
    // buggy

    // move str right after '='
    while (**str != '=')
        *str = *str + 1;
    *str = *str + 1;
    
    GArray *elmts_vec = g_array_sized_new(true, false, sizeof(ffi_type *), 4);
    while (**str != ')') {
        ffi_type *ty = getType(str, true, NULL, ptr_to_free);
        g_array_append_val(elmts_vec, ty);
    }
    // move str right after ')'
    *str = *str + 1;

    ffi_type **union_elmt = (ffi_type **)mycalloc(2, sizeof(ffi_type *),
                                                  ptr_to_free);
    ffi_type *one_elmt = (ffi_type *)mycalloc(1, sizeof(ffi_type), ptr_to_free);
    union_elmt[0] = one_elmt;

    ffi_type *union_type = (ffi_type *)mycalloc(1, sizeof(ffi_type),
                                                ptr_to_free);
    // union_type->size = union_type->alignment = 0;
    union_type->type = FFI_TYPE_STRUCT;
    union_type->elements = union_elmt;
    
    ffi_type **elmts = (ffi_type **)g_array_free(elmts_vec, false);
    for (ffi_type *i = elmts[0]; i != NULL; ++i) {
        xx_fi_cif cif;
        if (my_xx_fi_prep_cif(&cif, XX_FI_DEFAULT_ABI, 0, i, NULL, 0) ==
            FFI_OK) {
            if (i->alignment > one_elmt->alignment)
                one_elmt->alignment = i->alignment;
            
            if (i->size > one_elmt->size) {
                one_elmt->size = i->size;
                if (i->type == FFI_TYPE_STRUCT) {
                    one_elmt->type = i->type;
                    one_elmt->elements = i->elements;
                } else {
                    one_elmt->type = FFI_TYPE_UINT64; // works for my test cases
                    one_elmt->elements = NULL;
                }
            }
        }
    }
    g_free(elmts);
    return union_type;
}

static
ffi_type *getType(const char **str, bool arrAsAggr, bool *isBlockType,
                  GArray *ptr_to_free) {
    ffi_type *ty;
    char ch = **str;
    *str = *str + 1;
    switch (ch) {
    case 'c': // char
        ty = &ffi_type_schar;
        break;

    case 'i': // int
        ty = &ffi_type_sint;
        break;

    case 's': // short
        ty = &ffi_type_sshort;
        break;

    case 'l': // long
        // l is treated as a 32-bit quantity on 64-bit programs.
        ty = &ffi_type_sint32;
        break;

    case 'q': // long long
        ty = &ffi_type_slong;
        break;

    case 'C': // u char
        ty = &ffi_type_uchar;
        break;

    case 'I': // u int
        ty = &ffi_type_uint;
        break;

    case 'S': // u short
        ty = &ffi_type_ushort;
        break;

    case 'L': // u long
        ty = &ffi_type_uint32;
        break;

    case 'Q': // u long long
        ty = &ffi_type_ulong;
        break;

    case 'f': // float
        ty = &ffi_type_float;
        break;

    case 'd': // double
        ty = &ffi_type_double;
        break;

    case 'B': // bool
        ty = &ffi_type_uint8;
        break;

    case 'v': // void
        ty = &ffi_type_void;
        break;

    case '@': // object (whether statically typed or typed `id`)
        if (**str == '"') {
            *str = *str + 1;
            // e.g., `@"UserDefinedTypeName"`
            while (**str != '"')
                *str = *str + 1;
            *str = *str + 1;
        } else if (**str == '?') {
            // block has type encoding as `@?`
            if (NULL != isBlockType)
                *isBlockType = true;
            *str = *str + 1;
            // e.g., `@?<v@?@@"NSError">`
            // the nested type encoding may be useful
            if (**str == '<') {
                while (**str != '>')
                    *str = *str + 1;
                *str = *str + 1;
            }
        }     // fall through
    case '*': // char *
    case '#': // class object
    case ':': // selector
        ty = &ffi_type_pointer;
        break;

    case '^': // pointer to some type, so it is followed by some type
        ty = &ffi_type_pointer;
        moveFwdAType(str);
        break;

    case '[': // array
        ty = getArrayType(str, arrAsAggr, ptr_to_free);
        break;

    case '{': // structure
        ty = getStructType(str, ptr_to_free);
        break;

    case '(': // union
        ty = getUnionType(str, ptr_to_free);
        break;
    case 'r': // const
    case 'n': // in
    case 'N': // inout
    case 'o': // out
    case 'O': // bycopy
    case 'R': // byref
    case 'V': // oneway
        ty = getType(str, arrAsAggr, isBlockType, ptr_to_free);
        break;

    // TODO: bit field not handled. Currently it leads to abort()
    case 'b': // bit field
    case '?': // unknown type (function pointer is like `^?`)
    default:
        dbgf("encounter invalid char: %s\n", *str - 1);
        abort();
    }
    return ty;
}

static
bool handle(const char *types, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo,
            unsigned nfixed) {
    // if the string is null (function unimplemented?), do nothing
    if (*types == '\0')
        return false;
    
    bool ret = true;
    const char **str = &types;
    GArray *ptr_to_free = g_array_new(true, false, sizeof(void *));

    uint64_t blkMask = 0;
    bool retIsBlock = false;
    ffi_type *rtype = getType(str, false, &retIsBlock, ptr_to_free);
    // move str forward
    strtol(*str, (char **)str, 10);
    if (retIsBlock) {
        blkMask |= (((uint64_t)1) << 63);
    }
    
    GArray *atypes_vec = g_array_sized_new(true, false, sizeof(ffi_type *), 4);
    unsigned arg_cnt = 0;
    while (**str != '\0') {
        bool argIsBlock = false;
        ffi_type *ty = getType(str, false, &argIsBlock, ptr_to_free);
        g_array_append_val(atypes_vec, ty);
        strtol(*str, (char **)str, 10);
        if (argIsBlock) {
            blkMask |= (((uint64_t)1) << arg_cnt);
        }
        ++arg_cnt;
    }

    const unsigned nargs = (unsigned)atypes_vec->len;
    ffi_type **atypes = (ffi_type **)g_array_free(atypes_vec, false);
    g_array_append_val(ptr_to_free, atypes);
    
    if (NULL != xxInfo) {
        memset(xxInfo, 0, sizeof(ABIFnInfo));
        xxInfo->nargs = nargs;
        // xxInfo->ainfo = (ABIArgInfo *)mycalloc(nargs, sizeof(ABIArgInfo));
        // ABIFnInfo shoule live longer than TyEnHandler
        xxInfo->ainfo = (ABIArgInfo *)g_malloc0(nargs * sizeof(ABIArgInfo));

        xx_fi_cif xxcif;
        if (my_xx_fi_prep_cif(&xxcif, XX_FI_DEFAULT_ABI, nargs, rtype, atypes,
                              xxInfo) == FFI_OK) {
            if (blkMask & (1ULL << 63))
                xxInfo->rinfo.mask |= TE_MASK_BLOCK;
            for (unsigned i = 0; i != nargs; ++i) {
                if (blkMask & (1ULL << i))
                    xxInfo->ainfo[i].mask |= TE_MASK_BLOCK;
            }
        } else {
            dbgf("%s", "prep_xx_cif failed!\n");
            g_free(xxInfo->ainfo);
            ret = false;
            goto free_ptr;
        }
    }

    if (NULL != aaInfo) {
        memset(aaInfo, 0, sizeof(ABIFnInfo));
        aaInfo->nargs = nargs;
        // aaInfo->ainfo = (ABIArgInfo *)mycalloc(nargs, sizeof(ABIArgInfo));
        aaInfo->ainfo = (ABIArgInfo *)g_malloc0(nargs * sizeof(ABIArgInfo));

        aa_fi_cif aacif;
        if (nfixed != InvalidValue) {
            if (my_aa_fi_prep_cif_var(&aacif, AA_FI_DEFAULT_ABI, nfixed, nargs,
                                      rtype, atypes, aaInfo) != FFI_OK) {
                dbgf("%s", "prep_aa_cif_var failed!\n");
                g_free(aaInfo->ainfo);
                ret = false;
                goto free_ptr;
            }
        } else {
            if (my_aa_fi_prep_cif(&aacif, AA_FI_DEFAULT_ABI, nargs, rtype,
                                  atypes, aaInfo) != FFI_OK) {
                dbgf("%s", "prep_aa_cif failed!\n");
                g_free(aaInfo->ainfo);
                ret = false;
                goto free_ptr;
            }
        }
        if (blkMask & (1ULL << 63))
            aaInfo->rinfo.mask |= TE_MASK_BLOCK;
        for (unsigned i = 0; i != nargs; ++i) {
            if (blkMask & (1ULL << i))
                aaInfo->ainfo[i].mask |= TE_MASK_BLOCK;
        }
    }
    
free_ptr:
    for (void **i = (void **)ptr_to_free->data; *i; ++i) {
        g_free(*i);
    }
    g_array_free(ptr_to_free, true);
    
    return ret;
}

static __unused
bool handleRet(const char *types, ABIFnInfo *aaInfo, ABIFnInfo *xxInfo) {
    memset(xxInfo, 0, sizeof(ABIFnInfo));
    memset(aaInfo, 0, sizeof(ABIFnInfo));

    if (*types == '\0')
        return false;
    
    bool ret = true;
    const char **str = &types;
    GArray *ptr_to_free = g_array_new(true, false, sizeof(void *));
    
    bool retIsBlock = false;
    ffi_type *rtype = getType(str, false, &retIsBlock, ptr_to_free);
    if (retIsBlock) {
        aaInfo->rinfo.mask |= TE_MASK_BLOCK;
        xxInfo->rinfo.mask |= TE_MASK_BLOCK;
    }

    xx_fi_cif xxcif;
    if (my_xx_fi_prep_cif(&xxcif, XX_FI_DEFAULT_ABI, 0, rtype, NULL, xxInfo) !=
        FFI_OK) {
        dbgf("%s", "prep_xx_cif failed\n");
        ret = false;
        goto free_ptr;
    }

    aa_fi_cif aacif;
    if (my_aa_fi_prep_cif(&aacif, AA_FI_DEFAULT_ABI, 0, rtype, NULL, aaInfo) !=
        FFI_OK) {
        dbgf("%s", "prep_aa_cif failed\n");
        ret = false;
        goto free_ptr;
    }
    
free_ptr:
    for (void **i = (void **)ptr_to_free->data; *i; ++i) {
        g_free(*i);
    }
    g_array_free(ptr_to_free, true);
    
    return ret;
}

static
unsigned getNumTypes(const char *types) {
    const char **str = &types;
    GArray *ptr_to_free = g_array_new(true, false, sizeof(void *));
    
    unsigned arg_cnt = 0;
    while (**str != '\0') {
        getType(str, false, NULL, ptr_to_free);
        ++arg_cnt;
    }
    
    for (void **i = (void **)ptr_to_free->data; *i; ++i) {
        g_free(*i);
    }
    g_array_free(ptr_to_free, true);
    
    return arg_cnt;
}

// need to release ABIFnInfo::ainfo after use
bool get_fn_info_from_types(const char *types, ABIFnInfo *aaInfo,
                            ABIFnInfo *xxInfo) {
    return handle(types, aaInfo, xxInfo, InvalidValue);
}

bool get_vari_fn_info_from_types(const char *types, unsigned nfixed,
                                 ABIFnInfo *aaInfo, ABIFnInfo *xxInfo) {
    return handle(types, aaInfo, xxInfo, nfixed);
}

bool parse_type_codes(const char *types, unsigned nfixed, ABIFnInfo *aaInfo,
                      ABIFnInfo *xxInfo) {
    unsigned nvari = getNumTypes(types);
    unsigned len = 1 + nfixed + nvari; // ret + fix + var
    char final_types[len + 1];
    memset(final_types, '*', len);
    final_types[len] = '\0';
    return handle(types, aaInfo, xxInfo, nfixed);
}
