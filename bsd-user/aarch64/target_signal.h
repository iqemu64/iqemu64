#ifndef TARGET_SIGNAL_H
#define TARGET_SIGNAL_H

#include "cpu.h"

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
	abi_ulong ss_sp;
	abi_long ss_flags;
	abi_ulong ss_size;
} target_stack_t;

static inline abi_ulong get_sp_from_cpustate(CPUARMState *state)
{
    return state->xregs[31];
}

#define TARGET_MINSIGSTKSZ 2048
#define TARGET_SIGSTKSZ 8192

#endif /* TARGET_SIGNAL_H */
