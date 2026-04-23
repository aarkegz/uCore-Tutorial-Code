#include "signal.h"
#include "defs.h"
#include "proc.h"
#include "trap.h"

int signal_check_error(uint32 signals)
{
	if (signals & SIGINT)
		return -2;
	if (signals & SIGILL)
		return -4;
	if (signals & SIGABRT)
		return -6;
	if (signals & SIGFPE)
		return -8;
	if (signals & SIGKILL)
		return -9;
	if (signals & SIGSEGV)
		return -11;
	return 0;
}

void signal_actions_init(struct SignalActions *actions)
{
	for (int i = 0; i <= MAX_SIG; i++) {
		actions->table[i].handler = 0;
		actions->table[i].mask = SIGNAL_DEFAULT_MASK;
		actions->table[i]._pad = 0;
	}
}

void current_add_signal(uint32 signal)
{
	struct proc *p = curr_proc();
	p->signals |= signal;
}

static void call_kernel_signal_handler(uint32 signal)
{
	struct proc *p = curr_proc();
	if (signal == SIGSTOP) {
		p->frozen = 1;
		p->signals &= ~SIGSTOP;
	} else if (signal == SIGCONT) {
		if (p->signals & SIGCONT) {
			p->signals &= ~SIGCONT;
			p->frozen = 0;
		}
	} else {
		p->killed = 1;
	}
}

static void call_user_signal_handler(int sig, uint32 signal)
{
	struct proc *p = curr_proc();
	uint64 handler = p->signal_actions.table[sig].handler;
	if (handler != 0) {
		/* User handler */
		p->handling_sig = sig;
		p->signals &= ~signal;

		/* Backup trapframe */
		if (p->trap_ctx_backup == NULL) {
			p->trap_ctx_backup =
				(struct trapframe *)kalloc();
		}
		if (p->trap_ctx_backup) {
			*p->trap_ctx_backup = *p->trapframe;
		}

		/* Modify trapframe to jump to handler */
		p->trapframe->epc = handler;
		/* Pass signal number as argument (a0) */
		p->trapframe->a0 = sig;
	}
}

static void check_pending_signals(void)
{
	struct proc *p = curr_proc();
	for (int sig = 0; sig <= MAX_SIG; sig++) {
		uint32 signal = 1U << sig;
		if ((p->signals & signal) &&
		    !(p->signal_mask & signal)) {
			int masked = 1;
			if (p->handling_sig == -1) {
				masked = 0;
			} else {
				int handling = p->handling_sig;
				if (!(p->signal_actions.table[handling]
					      .mask &
				      signal)) {
					masked = 0;
				}
			}
			if (!masked) {
				if (signal == SIGKILL ||
				    signal == SIGSTOP ||
				    signal == SIGCONT ||
				    signal == SIGDEF) {
					call_kernel_signal_handler(signal);
				} else {
					call_user_signal_handler(sig,
								 signal);
					return;
				}
			}
		}
	}
}

void handle_signals(void)
{
	struct proc *p = curr_proc();
	while (1) {
		check_pending_signals();
		if (!p->frozen || p->killed) {
			break;
		}
		yield();
	}
}

int check_signals_error_of_current(void)
{
	struct proc *p = curr_proc();
	return signal_check_error(p->signals);
}
