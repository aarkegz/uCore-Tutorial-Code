#ifndef LOADER_H
#define LOADER_H

#include "const.h"
#include "riscv.h"
#include "types.h"

struct proc;

int finished();
void loader_init();
int run_all_app();
pagetable_t bin_loader(uint64 start, uint64 end, struct proc *p);
pagetable_t elf_loader(uint64 start, uint64 end, struct proc *p);

#define BASE_ADDRESS (0x1000)
#define USTACK_SIZE (PAGE_SIZE)
#define KSTACK_SIZE (PAGE_SIZE)
#define TRAP_PAGE_SIZE (PAGE_SIZE)

#endif // LOADER_H
