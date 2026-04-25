#include "loader.h"
#include "defs.h"
#include "elf.h"
#include "file.h"
#include "trap.h"

extern char INIT_PROC[];

// Load a flat binary from inode (file system based).
int bin_loader(struct inode *ip, struct proc *p)
{
	ivalid(ip);
	void *page;
	uint64 length = ip->size;
	uint64 va_start = BASE_ADDRESS;
	uint64 va_end = PGROUNDUP(BASE_ADDRESS + length);
	for (uint64 va = va_start, off = 0; va < va_end;
	     va += PGSIZE, off += PAGE_SIZE) {
		page = kalloc();
		if (page == 0) {
			panic("bin_loader: kalloc fail");
		}
		readi(ip, 0, (uint64)page, off, PAGE_SIZE);
		if (off + PAGE_SIZE > length) {
			memset(page + (length - off), 0,
			       PAGE_SIZE - (length - off));
		}
		if (mappages(p->pagetable, va, PGSIZE, (uint64)page,
			     PTE_U | PTE_R | PTE_W | PTE_X) != 0)
			panic("bin_loader: mappages fail");
	}

	p->max_page = va_end / PAGE_SIZE;
	p->ustack_base = va_end + PAGE_SIZE;
	// alloc main thread
	if (allocthread(p, va_start, 1) != 0) {
		panic("proc %d alloc main thread failed!", p->pid);
	}
	debugf("bin loader fin");
	return 0;
}

// Load an ELF-format application into a new address space.
// Maps each PT_LOAD segment with permissions from ELF headers,
// adds a guard page before the user stack.
pagetable_t elf_loader(uint64 start, uint64 end, struct proc *p)
{
	pagetable_t pg = uvmcreate((uint64)p->trapframe);

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)start;
	if (ehdr->e_machine != EM_RISCV) {
		panic("elf_loader: not RISC-V ELF");
	}

	uint64 max_end_va = 0;
	for (int i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *ph = (Elf64_Phdr *)(start + ehdr->e_phoff +
						 i * ehdr->e_phentsize);
		if (ph->p_type != PT_LOAD)
			continue;

		uint64 va_start = PGROUNDDOWN(ph->p_vaddr);
		uint64 va_end = PGROUNDUP(ph->p_vaddr + ph->p_memsz);
		uint64 file_offset = ph->p_offset;
		uint64 file_size = ph->p_filesz;

		int perm = PTE_U;
		if (ph->p_flags & PF_R)
			perm |= PTE_R;
		if (ph->p_flags & PF_W)
			perm |= PTE_W;
		if (ph->p_flags & PF_X)
			perm |= PTE_X;

		// Allocate and map pages for this segment
		for (uint64 va = va_start; va < va_end; va += PGSIZE) {
			char *mem = kalloc();
			if (mem == 0)
				panic("elf_loader: kalloc fail");
			memset(mem, 0, PGSIZE);

			// Copy file data into this page
			uint64 page_offset = va - ph->p_vaddr;
			if (page_offset < file_size) {
				uint64 copy_len = PGSIZE;
				if (page_offset + copy_len > file_size)
					copy_len = file_size - page_offset;
				memmove(mem,
					(void *)(start + file_offset +
						 page_offset),
					copy_len);
			}

			if (mappages(pg, va, PGSIZE, (uint64)mem, perm) != 0)
				panic("elf_loader: mappages fail");
		}

		if (va_end > max_end_va)
			max_end_va = va_end;
	}

	// Guard page: skip one page after the highest segment
	uint64 ustack_bottom = max_end_va + PAGE_SIZE;
	uint64 ustack_top = ustack_bottom + USTACK_SIZE;

	char *stack_mem = kalloc();
	if (stack_mem == 0)
		panic("elf_loader: kalloc stack fail");
	memset(stack_mem, 0, PGSIZE);
	if (mappages(pg, ustack_bottom, USTACK_SIZE, (uint64)stack_mem,
		     PTE_U | PTE_R | PTE_W) != 0)
		panic("elf_loader: mappages stack fail");

	p->pagetable = pg;
	p->ustack = ustack_bottom;
	p->trapframe->epc = ehdr->e_entry;
	p->trapframe->sp = ustack_top;
	p->max_page = PGROUNDUP(ustack_top - 1) / PAGE_SIZE;
	p->program_brk = ustack_top;
	p->heap_bottom = ustack_top;
	return pg;
}

// load init app and init the corresponding `proc` structure.
int load_init_app()
{
	struct inode *ip;
	struct proc *p = allocproc();
	init_stdio(p);
	if ((ip = namei(INIT_PROC)) == 0) {
		errorf("invalid init proc name\n");
		return -1;
	}
	debugf("load init app %s", INIT_PROC);
	bin_loader(ip, p);
	iput(ip);
	char *argv[2];
	argv[0] = INIT_PROC;
	argv[1] = NULL;
	struct thread *t = &p->threads[0];
	t->trapframe->a0 = push_argv(p, argv);
	t->state = RUNNABLE;
	add_task(t);
	// Memory fence about fetching the instruction memory.
	// It is guaranteed that a subsequent instruction fetch must
	// observe all previous writes to the instruction memory.
	// Therefore, fence.i must be executed after we have loaded
	// the code of all apps into the instruction memory.
	asm volatile("fence.i");
	return 0;
}
