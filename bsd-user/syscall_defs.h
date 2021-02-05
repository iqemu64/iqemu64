/*      $OpenBSD: signal.h,v 1.19 2006/01/08 14:20:16 millert Exp $     */
/*      $NetBSD: signal.h,v 1.21 1996/02/09 18:25:32 christos Exp $     */

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)signal.h    8.2 (Berkeley) 1/21/94
 */

#define TARGET_NSIG     32              /* counting 0; could be 33 (mask is 1-32) */
                                        // libiqemu note: We don't think that POSIX realtime signals are implemented in the xnu.

#define TARGET_SIGHUP  1       /* hangup */
#define TARGET_SIGINT  2       /* interrupt */
#define TARGET_SIGQUIT 3       /* quit */
#define TARGET_SIGILL  4       /* illegal instruction (not reset when caught) */
#define TARGET_SIGTRAP 5       /* trace trap (not reset when caught) */
#define TARGET_SIGABRT 6       /* abort() */
#define TARGET_SIGIOT  SIGABRT /* compatibility */
#define TARGET_SIGEMT  7       /* EMT instruction */
#define TARGET_SIGFPE  8       /* floating point exception */
#define TARGET_SIGKILL 9       /* kill (cannot be caught or ignored) */
#define TARGET_SIGBUS  10      /* bus error */
#define TARGET_SIGSEGV 11      /* segmentation violation */
#define TARGET_SIGSYS  12      /* bad argument to system call */
#define TARGET_SIGPIPE 13      /* write on a pipe with no one to read it */
#define TARGET_SIGALRM 14      /* alarm clock */
#define TARGET_SIGTERM 15      /* software termination signal from kill */
#define TARGET_SIGURG  16      /* urgent condition on IO channel */
#define TARGET_SIGSTOP 17      /* sendable stop signal not from tty */
#define TARGET_SIGTSTP 18      /* stop signal from tty */
#define TARGET_SIGCONT 19      /* continue a stopped process */
#define TARGET_SIGCHLD 20      /* to parent on child stop or exit */
#define TARGET_SIGTTIN 21      /* to readers pgrp upon background tty read */
#define TARGET_SIGTTOU 22      /* like TTIN for output if (tp->t_local&LTOSTOP) */
#define TARGET_SIGIO   23      /* input/output possible signal */
#define TARGET_SIGXCPU 24      /* exceeded CPU time limit */
#define TARGET_SIGXFSZ 25      /* exceeded file size limit */
#define TARGET_SIGVTALRM 26    /* virtual time alarm */
#define TARGET_SIGPROF 27      /* profiling time alarm */
#define TARGET_SIGWINCH 28      /* window size changes */
#define TARGET_SIGINFO  29      /* information request */
#define TARGET_SIGUSR1 30       /* user defined signal 1 */
#define TARGET_SIGUSR2 31       /* user defined signal 2 */

/*
 * Language spec says we must list exactly one parameter, even though we
 * actually supply three.  Ugh!
 */
#define TARGET_SIG_DFL         (void (*)(int))0
#define TARGET_SIG_IGN         (void (*)(int))1
#define TARGET_SIG_ERR         (void (*)(int))-1

#define TARGET_SA_ONSTACK       0x0001  /* take signal on signal stack */
#define TARGET_SA_RESTART       0x0002  /* restart system on signal return */
#define TARGET_SA_RESETHAND     0x0004  /* reset to SIG_DFL when taking signal */
#define TARGET_SA_NODEFER       0x0010  /* don't mask the signal we're delivering */
#define TARGET_SA_NOCLDWAIT     0x0020  /* don't create zombies (assign to pid 1) */
#define TARGET_SA_USERTRAMP    0x0100  /* do not bounce off kernel's sigtramp */
#define TARGET_SA_NOCLDSTOP     0x0008  /* do not generate SIGCHLD on child stop */
#define TARGET_SA_SIGINFO       0x0040  /* generate siginfo_t */

/*
 * Flags for sigprocmask:
 */
#define TARGET_SIG_BLOCK       1       /* block specified signal set */
#define TARGET_SIG_UNBLOCK     2       /* unblock specified signal set */
#define TARGET_SIG_SETMASK     3       /* set specified signal set */

#define TARGET_BADSIG          SIG_ERR

#define TARGET_SS_ONSTACK       0x0001  /* take signals on alternate stack */
#define TARGET_SS_DISABLE       0x0004  /* disable taking signals on alternate stack */

#include "errno_defs.h"

struct target_iovec {
    abi_long iov_base;   /* Starting address */
    abi_long iov_len;   /* Number of bytes */
};

#define TARGET_NSIG_BPW       TARGET_ABI_BITS
#define TARGET_NSIG_WORDS  (TARGET_NSIG / TARGET_NSIG_BPW)

typedef struct {
    abi_ulong sig[TARGET_NSIG_WORDS];
} target_sigset_t;
typedef abi_long target_clock_t;

struct target_sigaction {
        abi_ulong _sa_handler;
        abi_ulong sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        abi_ulong sa_restorer;
#endif
        target_sigset_t sa_mask;
#ifdef TARGET_ARCH_HAS_KA_RESTORER
        abi_ulong ka_restorer;
#endif
};

#define TARGET_SI_MAX_SIZE    128

#if TARGET_ABI_BITS == 32
#define TARGET_SI_PREAMBLE_SIZE (3 * sizeof(int))
#else
#define TARGET_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#define TARGET_SI_PAD_SIZE ((TARGET_SI_MAX_SIZE - TARGET_SI_PREAMBLE_SIZE) / sizeof(int))

typedef union target_sigval {
    int sival_int;
    abi_ulong sival_ptr;
} target_sigval_t;

/* Within QEMU the top 16 bits of si_code indicate which of the parts of
 * the union in target_siginfo is valid. This only applies between
 * host_to_target_siginfo_noswap() and tswap_siginfo(); it does not
 * appear either within host siginfo_t or in target_siginfo structures
 * which we get from the guest userspace program. (The Linux kernel
 * does a similar thing with using the top bits for its own internal
 * purposes but not letting them be visible to userspace.)
 */
#define QEMU_SI_KILL 0
#define QEMU_SI_TIMER 1
#define QEMU_SI_POLL 2
#define QEMU_SI_FAULT 3
#define QEMU_SI_CHLD 4

typedef struct target_siginfo {
    int si_signo;
    int si_errno;
    int si_code;

    union {
        int _pad[TARGET_SI_PAD_SIZE];

        /* kill() */
        struct {
            pid_t _pid;        /* sender's pid */
            uid_t _uid;        /* sender's uid */
        } _kill;

        /* POSIX.1b timers */
        struct {
            unsigned int _timer1;
            unsigned int _timer2;
        } _timer;

        /* POSIX.1b signals */
        struct {
            pid_t _pid;        /* sender's pid */
            uid_t _uid;        /* sender's uid */
            target_sigval_t _sigval;
        } _rt;

        /* SIGCHLD */
        struct {
            pid_t _pid;        /* which child */
            uid_t _uid;        /* sender's uid */
            int _status;        /* exit code */
        } _sigchld;

        /* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
        struct {
            abi_ulong _addr; /* faulting insn/memory ref. */
        } _sigfault;

        /* SIGPOLL */
        struct {
            int _band;    /* POLL_IN, POLL_OUT, POLL_MSG */
        } _sigpoll;
    } _sifields;
} target_siginfo_t;

/*
 * si_code values
 * Digital reserves positive values for kernel-generated signals.
 */
#define TARGET_SI_USER        0    /* sent by kill, sigsend, raise */
#define TARGET_SI_KERNEL    0x80    /* sent by the kernel from somewhere */
#define TARGET_SI_QUEUE    -1        /* sent by sigqueue */
#define TARGET_SI_TIMER -2              /* sent by timer expiration */
#define TARGET_SI_MESGQ    -3        /* sent by real time mesq state change */
#define TARGET_SI_ASYNCIO    -4    /* sent by AIO completion */
#define TARGET_SI_SIGIO    -5        /* sent by queued SIGIO */

/*
 * si_code values
 * Digital reserves positive values for kernel-generated signals.
 */
#define TARGET_SI_USER        0    /* sent by kill, sigsend, raise */
#define TARGET_SI_KERNEL    0x80    /* sent by the kernel from somewhere */
#define TARGET_SI_QUEUE    -1        /* sent by sigqueue */
#define TARGET_SI_TIMER -2              /* sent by timer expiration */
#define TARGET_SI_MESGQ    -3        /* sent by real time mesq state change */
#define TARGET_SI_ASYNCIO    -4    /* sent by AIO completion */
#define TARGET_SI_SIGIO    -5        /* sent by queued SIGIO */

/*
 * SIGILL si_codes
 */
#define TARGET_ILL_ILLOPC    (1)    /* illegal opcode */
#define TARGET_ILL_ILLOPN    (2)    /* illegal operand */
#define TARGET_ILL_ILLADR    (3)    /* illegal addressing mode */
#define TARGET_ILL_ILLTRP    (4)    /* illegal trap */
#define TARGET_ILL_PRVOPC    (5)    /* privileged opcode */
#define TARGET_ILL_PRVREG    (6)    /* privileged register */
#define TARGET_ILL_COPROC    (7)    /* coprocessor error */
#define TARGET_ILL_BADSTK    (8)    /* internal stack error */
#ifdef TARGET_TILEGX
#define TARGET_ILL_DBLFLT       (9)     /* double fault */
#define TARGET_ILL_HARDWALL     (10)    /* user networks hardwall violation */
#endif

/*
 * SIGFPE si_codes
 */
#define TARGET_FPE_INTDIV      (1)  /* integer divide by zero */
#define TARGET_FPE_INTOVF      (2)  /* integer overflow */
#define TARGET_FPE_FLTDIV      (3)  /* floating point divide by zero */
#define TARGET_FPE_FLTOVF      (4)  /* floating point overflow */
#define TARGET_FPE_FLTUND      (5)  /* floating point underflow */
#define TARGET_FPE_FLTRES      (6)  /* floating point inexact result */
#define TARGET_FPE_FLTINV      (7)  /* floating point invalid operation */
#define TARGET_FPE_FLTSUB      (8)  /* subscript out of range */
#define TARGET_FPE_FLTUNK      (14) /* undiagnosed fp exception */
#define TARGET_NSIGFPE         15

/*
 * SIGSEGV si_codes
 */
#define TARGET_SEGV_MAPERR     (1)  /* address not mapped to object */
#define TARGET_SEGV_ACCERR     (2)  /* invalid permissions for mapped object */
#define TARGET_SEGV_BNDERR     (3)  /* failed address bound checks */

/*
 * SIGBUS si_codes
 */
#define TARGET_BUS_ADRALN       (1)    /* invalid address alignment */
#define TARGET_BUS_ADRERR       (2)    /* non-existent physical address */
#define TARGET_BUS_OBJERR       (3)    /* object specific hardware error */
/* hardware memory error consumed on a machine check: action required */
#define TARGET_BUS_MCEERR_AR    (4)
/* hardware memory error detected in process but not consumed: action optional*/
#define TARGET_BUS_MCEERR_AO    (5)

/*
 * SIGTRAP si_codes
 */
#define TARGET_TRAP_BRKPT    (1)    /* process breakpoint */
#define TARGET_TRAP_TRACE    (2)    /* process trace trap */
#define TARGET_TRAP_BRANCH      (3)     /* process taken branch trap */
#define TARGET_TRAP_HWBKPT      (4)     /* hardware breakpoint/watchpoint */

