#include "trap.h"
#include "defs.h"
#include "loader.h"
#include "syscall.h"

extern char __alltraps[];

void trap_init(void)
{
	w_stvec((uint64)__alltraps);
}

struct trap_context *trap_handler(struct trap_context *cx)
{
	uint64 cause = r_scause();
	if (cause == UserEnvCall) {
		cx->sepc += 4;
		cx->x[10] = syscall(cx->x[17], cx->x[10], cx->x[11], cx->x[12]);
	} else if (cause == StoreAccessFault || cause == StorePageFault) {
		errorf("PageFault in application, kernel killed it.");
		run_next_app();
	} else if (cause == LoadAccessFault || cause == LoadPageFault) {
		errorf("LoadFault in application, kernel killed it.");
		run_next_app();
	} else if (cause == IllegalInstruction) {
		errorf("IllegalInstruction in application, kernel killed it.");
		run_next_app();
	} else {
		errorf("Unsupported trap: cause=%p, stval=%p!", cause, r_stval());
		panic("trap_handler: unknown trap");
	}
	return cx;
}
