#ifndef TRAP_H
#define TRAP_H

#include "types.h"

// Trap context: saves all general-purpose registers + sstatus + sepc
// Aligned with rCore's TrapContext structure
struct trap_context {
    uint64 x[32];  // 32 general-purpose registers
    uint64 sstatus;
    uint64 sepc;
};

enum Exception {
    InstructionMisaligned = 0,
    InstructionAccessFault = 1,
    IllegalInstruction = 2,
    Breakpoint = 3,
    LoadMisaligned = 4,
    LoadAccessFault = 5,
    StoreMisaligned = 6,
    StoreAccessFault = 7,
    UserEnvCall = 8,
    SupervisorEnvCall = 9,
    MachineEnvCall = 11,
    InstructionPageFault = 12,
    LoadPageFault = 13,
    StorePageFault = 15,
};

void trap_init();
struct trap_context *trap_handler(struct trap_context *cx);

#endif // TRAP_H
