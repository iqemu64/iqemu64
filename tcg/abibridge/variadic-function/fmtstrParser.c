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

//===----------------------------------------------------------------------===//
//
// A platform-dependent format string parser.
//
// The syntax for a printf format placeholder is
// %[parameter][flags][width][.precision][length]type
//
// see https://en.wikipedia.org/wiki/Printf_format_string for details
// https://pubs.opengroup.org/onlinepubs/009695399/functions/printf.html
//
// The syntax for a scanf format placeholder is
// %[parameter][*][width][length]specifier
//
//===----------------------------------------------------------------------===//

#include "tcg/ABIArgInfo.h"

#include "glib.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NDEBUG

#define dbgs puts
#define dbgf printf

#else

#define dbgs(x)
#define dbgf(format, args...)

#endif


// I wish there were no -Wformat warning in the format string

enum {
    FIELD_FLAGS_PARSED =        (1 << 0),
    FIELD_WIDTH_PARSED =        (1 << 1),
    FIELD_PRECISION_PARSED =    (1 << 2),
    FIELD_LENGTH_PARSED =       (1 << 3),
    
    PRINTF_FIELD_ALL_PARSED = FIELD_FLAGS_PARSED | FIELD_WIDTH_PARSED |
        FIELD_PRECISION_PARSED |FIELD_LENGTH_PARSED,
    SCANF_FIELD_ALL_PARSED = FIELD_WIDTH_PARSED | FIELD_LENGTH_PARSED,
};

/**
 * parse_parameter_field:
 * @str: points to the next character behind '%'
 * @n: the position of the parameter to display using this format specifier,
 *     or 0 if this is not a parameter field
 * @return: points to the next character behind this field
 *
 * position arguments in format strings start counting at 1
 */
inline static
const char *parse_parameter_field(const char *str, unsigned *n) {
    *n = 0;
    if (!isdigit(*str))
        return str;
    
    char *ed;
    *n = (unsigned)strtol(str, &ed, 10);
    if ('$' == *ed)
        return ed + 1;
    *n = 0;
    return str;
}

/**
 * parse_flags_field:
 * @str: points to the next character behind last field
 * @return: points to the next character behind this field
 */
inline static
const char *parse_flags_field(const char *str, unsigned *field_mask) {
    const char flags[] = "-+ 0'#";
    while (NULL != strchr(flags, *str)) {
        ++str;
        *field_mask |= FIELD_FLAGS_PARSED;
    }
    return str;
}

/**
 * parse_width_field:
 * @str: points to the next character behind last field
 * @asterisk: whether there is an argument indicating the width
 * @numbered: does this fmtstr use numbered argument conversion specifications
 * @pos_ell: OUT. the position of the argument in the argument list, or 0
 * @return: points to the next character behind this field
 */
inline static
const char *parse_width_field(const char *str, bool *asterisk, bool numbered,
                              unsigned *pos_ell, unsigned *field_mask) {
    *asterisk = false;
    *pos_ell = 0;
    if ('*' == *str) {
        *asterisk = true;
        ++str;
        *field_mask |= FIELD_WIDTH_PARSED;
        if (numbered) {
            char *ed;
            *pos_ell = (unsigned)strtol(str, &ed, 10);
            if ('$' == *ed)
                return ed + 1;
        }
        return str;
    }
    
    while (isdigit(*str)) {
        ++str;
        *field_mask |= FIELD_WIDTH_PARSED;
    }
    return str;
}

/**
 * parse_precision_field:
 * @str: points to the next character behind last field
 * @asterisk: whether there is an argument indicating the precision
 * @numbered: does this fmtstr use numbered argument conversion specifications
 * @pos_ell: OUT. the position of the argument in the argument list, or 0
 * @return: points to the next character behind this field
 */
inline static
const char *parse_precision_field(const char *str, bool *asterisk,
                                  bool numbered, unsigned *pos_ell,
                                  unsigned *field_mask) {
    *asterisk = false;
    *pos_ell = 0;
    if ('.' != *str)
        return str;
    
    *field_mask |= FIELD_PRECISION_PARSED;
    ++str;
    if ('*' == *str) {
        *asterisk = true;
        ++str;
        if (numbered) {
            char *ed;
            *pos_ell = (unsigned)strtol(str, &ed, 10);
            if ('$' == *ed)
                return ed + 1;
        }
        return str;
    }
    
    while (isdigit(*str)) {
        ++str;
    }
    return str;
}

/**
 * parse_length_field:
 * @str: points to the next character behind last field
 * @return: points to the next character behind this field
 */
inline static
const char *parse_length_field(const char *str, unsigned *field_mask) {
    const char length[] = "hlLzjtI3264q";
    while (NULL != strchr(length, *str)) {
        ++str;
        *field_mask |= FIELD_LENGTH_PARSED;
    }
    return str;
}

typedef enum {
    FS_TYPE_NONE = 0, // for '%' itself
    FS_TYPE_INT = 'i',
    FS_TYPE_UINT = 'I',
    FS_TYPE_DOUBLE = 'd',
    FS_TYPE_POINTER = '*',
    FS_TYPE_CHAR = 'c',
} arg_type;

/**
 * parse_type_field:
 * @str: points to the next character behind last field
 * @ty: the argument type
 * @return: points to the next character behind this field. Or NULL on failure.
 *
 * long double is identical to double in iOS
 * sizeof(long long) = 8 in iOS
 */
inline static
const char *parse_type_field(const char *str, arg_type *ty) {
    switch (*str) {
        case '%':
            *ty = FS_TYPE_NONE;
            break;
        case 'd':
        case 'i':
            *ty = FS_TYPE_INT;
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            *ty = FS_TYPE_UINT;
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            *ty = FS_TYPE_DOUBLE;
            break;
        case 's':
        case 'S':
        case '@': // Objective-C object
        case 'p':
        case 'n':
            *ty = FS_TYPE_POINTER;
            break;
        case 'c':
        case 'C':
            *ty = FS_TYPE_CHAR;
            break;
        case '{':
            // e.g., %{public}s
            while ('}' != *str)
                ++str;
            return parse_type_field(str + 1, ty);
            break;
        default:
            return NULL;
            dbgf("fmtstr parse type error: %s\n", str);
            g_assert_not_reached();
    }
    return str + 1;
}

inline static
const char *parse_specifier_field(const char *str, arg_type *ty) {
    switch (*str) {
        case '%':
            *ty = FS_TYPE_NONE;
            break;
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'f':
        case 'e':
        case 'g':
        case 'a':
        case 'c':
        case 's':
        case 'p':
        case 'n':
        case '[':
            *ty = FS_TYPE_POINTER;
            break;
        case '{':
            // e.g., %{public}s
            while ('}' != *str)
                ++str;
            return parse_specifier_field(str + 1, ty);
            break;
        default:
            return NULL;
            dbgf("scanf parse error: %s\n", str);
            g_assert_not_reached();
    }
    if ('[' == *str) {
        while (']' != *str)
            ++str;
    }
    return str + 1;
}

inline static
const char *move_behind_percent(const char *str) {
    while (*str != '\0') {
        if (*str == '%')
            return str + 1;
        ++str;
    }
    return str;
}



// The iOS ABI for functions that take a variable number of arguments is
// entirely different from the generic version.
//
// Stages A and B of the generic procedure call standard are performed as usual.
// In particular, even variadic aggregates larger than 16 bytes are passed via a
// reference to temporary memory allocated by the caller. After that, the fixed
// arguments are allocated to registers and stack slots as usual in iOS.
//
// The NSRN is then rounded up to the next multiple of 8 bytes, and each
// variadic argument is assigned to the appropriate number of 8-byte stack
// slots.
//
// The C language requires arguments smaller than int to be promoted before a
// call, but beyond that, unused bytes on the stack are not specified by iOS
// ABI.
//
// As a result of this change, the type va_list is an alias for char * rather
// than for the struct type specified in the generic PCS. It is also not in the
// std namespace when compiling C++ code.


bool get_fn_info_from_types(const char *str, ABIFnInfo *aaInfo,
                            ABIFnInfo *xxInfo);

bool get_vari_fn_info_from_types(const char *str, unsigned nfixed,
                                 ABIFnInfo *aaInfo, ABIFnInfo *xxInfo);

bool parse_fmtstr(const char *str, unsigned nfixed, ABIFnInfo *aaInfo,
                  ABIFnInfo *xxInfo);

bool parse_fmtstr(const char *str, unsigned nfixed, ABIFnInfo *aaInfo,
                  ABIFnInfo *xxInfo) {
    // Create a type encoding string according to the format string.
    // The digits are omitted because currently they are not in use.
    
    // set return type to pointer can cope with most situations
    GString *types = g_string_sized_new(24);
    g_string_append_c_inline(types, (gchar)FS_TYPE_POINTER);
    for (unsigned i = 0; i < nfixed; ++i) {
        // Assert all fixed arguments can be viewed as of pointer type.
        g_string_append_c_inline(types, (gchar)FS_TYPE_POINTER);
    }
    
    bool new_round = true;
    unsigned field_mask = 0;
    unsigned pos_in_ellipsis = 0;
    unsigned idx_in_types = 0;
    unsigned width_idx_in_types = 0;
    unsigned precision_idx_in_types = 0;
    bool need_extra_param_for_width = false;
    bool need_extra_param_for_precision = false;
    for (;;) {
        if (new_round) {
            str = move_behind_percent(str);
            if ('\0' == *str)
                break;
            field_mask = 0;
        
            pos_in_ellipsis = 0;
            str = parse_parameter_field(str, &pos_in_ellipsis);
            idx_in_types = nfixed + pos_in_ellipsis;
            unsigned size = (unsigned)strlen(types->str);
            if (size < idx_in_types + 1)
                // padding types will be overwritten
                for (unsigned cnt = idx_in_types + 1 - size; cnt != 0; --cnt)
                    g_string_append_c_inline(types, FS_TYPE_POINTER);
        }
        
        if (!(field_mask & FIELD_FLAGS_PARSED))
            str = parse_flags_field(str, &field_mask);
        
        if (!(field_mask & FIELD_WIDTH_PARSED)) {
            need_extra_param_for_width = false;
            unsigned pos_in_ell_width = 0;
            str = parse_width_field(str, &need_extra_param_for_width,
                                    pos_in_ellipsis, &pos_in_ell_width,
                                    &field_mask);
            width_idx_in_types = nfixed + pos_in_ell_width;
            unsigned size = (unsigned)strlen(types->str);
            if (size < width_idx_in_types + 1)
                for (unsigned cnt = width_idx_in_types + 1 - size;
                     cnt != 0; --cnt)
                    g_string_append_c_inline(types, FS_TYPE_POINTER);
        }

        if (!(field_mask & FIELD_PRECISION_PARSED)) {
            need_extra_param_for_precision = false;
            unsigned pos_in_ell_precision = 0;
            str = parse_precision_field(str, &need_extra_param_for_precision,
                                        pos_in_ellipsis, &pos_in_ell_precision,
                                        &field_mask);
            precision_idx_in_types = nfixed + pos_in_ell_precision;
            unsigned size = (unsigned)strlen(types->str);
            if (size < precision_idx_in_types + 1)
                for (unsigned cnt = precision_idx_in_types + 1 - size;
                     cnt != 0; --cnt)
                    g_string_append_c_inline(types, FS_TYPE_POINTER);
        }

        if (!(field_mask & FIELD_LENGTH_PARSED))
            str = parse_length_field(str, &field_mask);
        
        arg_type ty;
        const char *result = parse_type_field(str, &ty);
        if (!result) {
            if (PRINTF_FIELD_ALL_PARSED == field_mask)
                abort();
            new_round = false;
            continue;
        }
        str = result;
        
        // we are done parsing one format placeholder
        if (0 != pos_in_ellipsis) {
            types->str[idx_in_types] = (char)ty;
            if (need_extra_param_for_width)
                types->str[width_idx_in_types] = (char)FS_TYPE_INT;
            if (need_extra_param_for_precision)
                types->str[precision_idx_in_types] = (char)FS_TYPE_INT;
        } else {
            if (need_extra_param_for_width)
                g_string_append_c_inline(types, FS_TYPE_INT);
            
            if (need_extra_param_for_precision)
                g_string_append_c_inline(types, FS_TYPE_INT);
            
            if (FS_TYPE_NONE != ty)
                g_string_append_c_inline(types, (gchar)ty);
        }
        new_round = true;
    }
    
    bool ret = get_vari_fn_info_from_types(types->str, nfixed, aaInfo, xxInfo);
    g_string_free(types, true);
    return ret;
}

bool parse_scanf_fmtstr(const char *str, unsigned idxOfFmt, ABIFnInfo *aaInfo,
                        ABIFnInfo *xxInfo);

bool parse_scanf_fmtstr(const char *str, unsigned idxOfFmt, ABIFnInfo *aaInfo,
                        ABIFnInfo *xxInfo) {
    // Create a type encoding string according to the format string.
    // The digits are omitted because currently they are not in use.
    
    // the return type of scanf family is int
    GString *types = g_string_sized_new(24);
    g_string_append_c_inline(types, FS_TYPE_INT);
    for (unsigned i = 0; i <= idxOfFmt; ++i) {
        g_string_append_c_inline(types, FS_TYPE_POINTER);
    }
    
    bool new_round = true;
    unsigned field_mask = 0;
    unsigned pos_in_ellipsis = 0;
    unsigned idx_in_types = 0;
    bool ignore = false;
    for (;;) {
        if (new_round) {
            str = move_behind_percent(str);
            if ('\0' == *str)
                break;
            field_mask = 0;
            
            pos_in_ellipsis = 0;
            str = parse_parameter_field(str, &pos_in_ellipsis);
            idx_in_types = idxOfFmt + pos_in_ellipsis + 1;
            unsigned size = (unsigned)strlen(types->str);
            if (size < idx_in_types + 1)
                for (unsigned cnt = idx_in_types + 1 - size; cnt != 0; --cnt)
                    g_string_append_c_inline(types, FS_TYPE_POINTER);
            
            ignore = false;
            if ('*' == *str) {
                ++str;
                ignore = true;
            }
        }
        
        if (!(field_mask & FIELD_WIDTH_PARSED)) {
            bool i_dont_care;
            unsigned i_dont_care_pos;
            str = parse_width_field(str, &i_dont_care, pos_in_ellipsis,
                                    &i_dont_care_pos, &field_mask);
        }
        
        if (!(field_mask & FIELD_LENGTH_PARSED))
            str = parse_length_field(str, &field_mask);
        
        arg_type ty;
        const char *result = parse_specifier_field(str, &ty);
        if (!result) {
            if (SCANF_FIELD_ALL_PARSED == field_mask)
                abort();
            new_round = false;
            continue;
        }
        str = result;
        
        if (0 != pos_in_ellipsis) {
            types->str[idx_in_types] = (char)ty;
        } else {
            if (!ignore && FS_TYPE_NONE != ty)
                g_string_append_c_inline(types, (gchar)ty);
        }
        new_round = true;
    }

    bool ret = get_vari_fn_info_from_types(types->str, idxOfFmt + 1,
                                           aaInfo, xxInfo);
    g_string_free(types, true);
    return ret;
}
