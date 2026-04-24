#ifndef TRAP_H
#define TRAP_H

#include "types.h"

// Trap frame: saved user registers and kernel state for trampoline.S
// Layout must match trampoline.S offsets exactly
struct trapframe {
    /* kernel bookkeeping */
    uint64 kernel_satp;   // 0:  kernel page table
    uint64 kernel_sp;     // 8:  top of process's kernel stack
    uint64 kernel_trap;   // 16: usertrap() address
    uint64 epc;           // 24: saved user sepc
    uint64 kernel_hartid; // 32: saved kernel tp
    /* 31 saved user registers (x1-x31, skipping x0) */
    uint64 ra;            // 40
    uint64 sp;            // 48
    uint64 gp;            // 56
    uint64 tp;            // 64
    uint64 t0;            // 72
    uint64 t1;            // 80
    uint64 t2;            // 88
    uint64 s0;            // 96
    uint64 s1;            // 104
    uint64 a0;            // 112
    uint64 a1;            // 120
    uint64 a2;            // 128
    uint64 a3;            // 136
    uint64 a4;            // 144
    uint64 a5;            // 152
    uint64 a6;            // 160
    uint64 a7;            // 168
    uint64 s2;            // 176
    uint64 s3;            // 184
    uint64 s4;            // 192
    uint64 s5;            // 200
    uint64 s6;            // 208
    uint64 s7;            // 216
    uint64 s8;            // 224
    uint64 s9;            // 232
    uint64 s10;           // 240
    uint64 s11;           // 248
    uint64 t3;            // 256
    uint64 t4;            // 264
    uint64 t5;            // 272
    uint64 t6;            // 280
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

enum Interrupt {
    SupervisorSoft = 1,
    SupervisorTimer = 5,
    SupervisorExternal = 9,
};

void trap_init();
void usertrapret();

#endif // TRAP_H
