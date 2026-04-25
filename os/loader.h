#ifndef LOADER_H
#define LOADER_H

#include "const.h"
#include "file.h"
#include "proc.h"
#include "riscv.h"
#include "types.h"

struct proc;

int load_init_app();
int bin_loader(struct inode *, struct proc *);
pagetable_t elf_loader(uint64 start, uint64 end, struct proc *p);

#define BASE_ADDRESS (0x1000)
#define USTACK_SIZE (PAGE_SIZE)
#define KSTACK_SIZE (PAGE_SIZE)
#define TRAP_PAGE_SIZE (PAGE_SIZE)

#endif // LOADER_H
