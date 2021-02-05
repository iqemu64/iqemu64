#ifndef exports_h
#define exports_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IQEMU_REG_PC        -1
#define IQEMU_REG_CPSR      -2

#define IQEMU_REG_X0        0
#define IQEMU_REG_X1        1
#define IQEMU_REG_X2        2
#define IQEMU_REG_X3        3
#define IQEMU_REG_X4        4
#define IQEMU_REG_X5        5
#define IQEMU_REG_X6        6
#define IQEMU_REG_X7        7
#define IQEMU_REG_X8        8
#define IQEMU_REG_X9        9
#define IQEMU_REG_X10       10
#define IQEMU_REG_X11       11
#define IQEMU_REG_X12       12
#define IQEMU_REG_X13       13
#define IQEMU_REG_X14       14
#define IQEMU_REG_X15       15
#define IQEMU_REG_X16       16
#define IQEMU_REG_X17       17
#define IQEMU_REG_X18       18
#define IQEMU_REG_X19       19
#define IQEMU_REG_X20       20
#define IQEMU_REG_X21       21
#define IQEMU_REG_X22       22
#define IQEMU_REG_X23       23
#define IQEMU_REG_X24       24
#define IQEMU_REG_X25       25
#define IQEMU_REG_X26       26
#define IQEMU_REG_X27       27
#define IQEMU_REG_X28       28
#define IQEMU_REG_X29       29
#define IQEMU_REG_FP        29
#define IQEMU_REG_X30       30
#define IQEMU_REG_LR        30
#define IQEMU_REG_SP        31

bool        iqemu_is_code_in_jit(uintptr_t v);
int *       iqemu_get_jmp_buf(int index);
void *      iqemu_get_current_thread_context(void);
bool        iqemu_need_emulation(uintptr_t v);
uintptr_t   iqemu_get_context_register(void *context, int num);
void        iqemu_set_context_register(void *context, int num, uintptr_t value);
bool        iqemu_get_CFI(int index, uintptr_t *sp, uintptr_t *fp);
void        iqemu_set_xloop_params_by_types(const char *types);
//
// This is an internal stub which does not follow standard ABI, do not use directly.
void        __iqemu_begin_emulation(void);

#define iqemu_begin_emulation(fnType, types, retvalue, ...)  \
    do {    \
        iqemu_set_xloop_params_by_types(types);      \
        retvalue = ((fnType)__iqemu_begin_emulation)(__VA_ARGS__); \
    } while(0);


#ifdef __cplusplus
}

#endif


#endif /* exports_h */
