#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"

#define MAX_SIG 31

/* Signal flags (bitmask) */
#define SIGDEF    (1U)
#define SIGHUP    (1U << 1)
#define SIGINT    (1U << 2)
#define SIGQUIT   (1U << 3)
#define SIGILL    (1U << 4)
#define SIGTRAP   (1U << 5)
#define SIGABRT   (1U << 6)
#define SIGBUS    (1U << 7)
#define SIGFPE    (1U << 8)
#define SIGKILL   (1U << 9)
#define SIGUSR1   (1U << 10)
#define SIGSEGV   (1U << 11)
#define SIGUSR2   (1U << 12)
#define SIGPIPE   (1U << 13)
#define SIGALRM   (1U << 14)
#define SIGTERM   (1U << 15)
#define SIGSTKFLT (1U << 16)
#define SIGCHLD   (1U << 17)
#define SIGCONT   (1U << 18)
#define SIGSTOP   (1U << 19)
#define SIGTSTP   (1U << 20)
#define SIGTTIN   (1U << 21)
#define SIGTTOU   (1U << 22)
#define SIGURG    (1U << 23)
#define SIGXCPU   (1U << 24)
#define SIGXFSZ   (1U << 25)
#define SIGVTALRM (1U << 26)
#define SIGPROF   (1U << 27)
#define SIGWINCH  (1U << 28)
#define SIGIO     (1U << 29)
#define SIGPWR    (1U << 30)
#define SIGSYS    (1U << 31)

/* Default mask value (SIGILL | SIGTRAP = 1<<4 | 1<<5 = 40) */
#define SIGNAL_DEFAULT_MASK 40

/* Signal action structure (aligned to 16 bytes, matches rCore layout) */
struct __attribute__((aligned(16))) SignalAction {
	uint64 handler; /* Signal handler address, 0 means default */
	uint32 mask; /* Signal mask during handler execution */
	uint32 _pad; /* Padding for alignment */
};

/* Signal actions table */
struct SignalActions {
	struct SignalAction table[MAX_SIG + 1];
};

/* Check if signal flags contain an error signal, return error code or 0 */
int signal_check_error(uint32 signals);

/* Initialize signal actions to defaults */
void signal_actions_init(struct SignalActions *actions);

/* Signal handling functions */
void handle_signals(void);
int check_signals_error_of_current(void);
void current_add_signal(uint32 signal);

#endif // SIGNAL_H
