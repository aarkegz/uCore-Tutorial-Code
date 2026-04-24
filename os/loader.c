#include "loader.h"
#include "defs.h"
#include "elf.h"
#include "trap.h"

static int app_num;
static uint64 *app_info_ptr;
extern char _app_num[];

// Count finished programs. If all apps exited, shutdown.
int finished()
{
	static int fin = 0;
	if (++fin >= app_num)
		panic("all apps over");
	return 0;
}

// Get user progs' infomation through pre-defined symbol in `link_app.S`
void loader_init()
{
	app_info_ptr = (uint64 *)_app_num;
	app_num = *app_info_ptr;
	app_info_ptr++;
	infof("[kernel] num_app = %d", app_num);
	for (int i = 0; i < app_num; i++) {
		infof("[kernel] app_%d [%p, %p)", i, app_info_ptr[1 + i],
		      app_info_ptr[2 + i]);
	}
}

pagetable_t bin_loader(uint64 start, uint64 end, struct proc *p)
{
	pagetable_t pg = uvmcreate();
	if (mappages(pg, TRAPFRAME, PGSIZE, (uint64)p->trapframe,
		     PTE_R | PTE_W) < 0) {
		panic("mappages fail");
	}
	if (!PGALIGNED(start)) {
		panic("user program not aligned, start = %p", start);
	}
	if (!PGALIGNED(end)) {
		warnf("Some kernel data maybe mapped to user, start = %p, end = %p",
		      start, end);
	}
	end = PGROUNDUP(end);
	uint64 length = end - start;
	if (mappages(pg, BASE_ADDRESS, length, start,
		     PTE_U | PTE_R | PTE_W | PTE_X) != 0) {
		panic("mappages fail");
	}
	p->pagetable = pg;
	uint64 ustack_bottom_vaddr = BASE_ADDRESS + length + PAGE_SIZE;
	if (USTACK_SIZE != PAGE_SIZE) {
		panic("Unsupported");
	}
	mappages(pg, ustack_bottom_vaddr, USTACK_SIZE, (uint64)kalloc(),
		 PTE_U | PTE_R | PTE_W | PTE_X);
	p->ustack = ustack_bottom_vaddr;
	p->trapframe->epc = BASE_ADDRESS;
	p->trapframe->sp = p->ustack + USTACK_SIZE;
	p->max_page = PGROUNDUP(p->ustack + USTACK_SIZE - 1) / PAGE_SIZE;
	p->program_brk = p->ustack + USTACK_SIZE;
	p->heap_bottom = p->ustack + USTACK_SIZE;
	return pg;
}

// Load an ELF-format application into a new address space.
// Maps each PT_LOAD segment with permissions from ELF headers,
// adds a guard page before the user stack.
pagetable_t elf_loader(uint64 start, uint64 end, struct proc *p)
{
	pagetable_t pg = uvmcreate();
	if (mappages(pg, TRAPFRAME, PGSIZE, (uint64)p->trapframe,
		     PTE_R | PTE_W) < 0) {
		panic("elf_loader: mappages trapframe fail");
	}

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
		     PTE_U | PTE_R | PTE_W | PTE_X) != 0)
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

// load all apps and init the corresponding `proc` structure.
int run_all_app()
{
	for (int i = 0; i < app_num; ++i) {
		struct proc *p = allocproc();
		uint64 start = app_info_ptr[i];
		uint64 end = app_info_ptr[i + 1];
		tracef("load app %d", i);
		if (is_elf(start)) {
			elf_loader(start, end, p);
		} else {
			bin_loader(start, end, p);
		}
		p->state = RUNNABLE;
	}
	asm volatile("fence.i");
	return 0;
}
