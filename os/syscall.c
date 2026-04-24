#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	debugf("sys_read fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDIN)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = 0;
	for (int i = 0; i < len && i < MAX_STR_LEN; ++i) {
		int c;
		do {
			c = consgetc();
			if (c == 0) {
				yield();
			}
		} while (c == 0);
		str[i] = c;
		size++;
	}
	copyout(p->pagetable, va, str, size);
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal tv;
	tv.sec = cycle / CPU_FREQ;
	tv.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	if (copyout(p->pagetable, (uint64)val, (char *)&tv, sizeof(TimeVal)) < 0)
		return -1;
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!\n");
	return fork();
}

uint64 sys_exec(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_exec %s\n", name);
	return exec(name);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);

	int id = get_id_by_name(name);
	if (id < 0)
		return -1;

	struct proc *np = allocproc();
	if (np == 0)
		return -1;

	// Free the stub page table created by allocproc since loader will
	// create its own.  Only TRAMPOLINE and TRAPFRAME are mapped (no user
	// pages), so uvmunmap with do_free=0 then freewalk is sufficient.
	uvmunmap(np->pagetable, TRAMPOLINE, 1, 0);
	uvmunmap(np->pagetable, TRAPFRAME, 1, 0);
	freewalk_all(np->pagetable);
	np->pagetable = 0;

	loader(id, np);
	np->parent = p;
	np->state = RUNNABLE;
	add_task(np);
	return np->pid;
}

uint64 sys_set_priority(long long prio)
{
	if (prio < 2)
		return -1;
	struct proc *p = curr_proc();
	p->priority = (uint64)prio;
	return (uint64)prio;
}


uint64 sys_sbrk(int n)
{
        uint64 addr;
        struct proc *p = curr_proc();
        addr = p->program_brk;
        if(growproc(n) < 0)
                return -1;
        return addr;
}



// TODO: add support for mmap and munmap syscall. (LAB1)
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
uint64 sys_mmap(uint64 start, uint64 len, uint64 port)
{
	struct proc *p = curr_proc();

	// Check: start must be page-aligned
	if (start % PGSIZE != 0)
		return -1;
	// Check: port must only use bits 0-2
	if (port & ~0x7)
		return -1;
	// Check: port must have at least one permission bit set
	if ((port & 0x7) == 0)
		return -1;
	// Check: len must be positive
	if (len == 0)
		return -1;

	// Check: all pages in [start, start+len) must be unmapped
	uint64 npages = (len + PGSIZE - 1) / PGSIZE;
	for (uint64 a = start; a < start + npages * PGSIZE; a += PGSIZE) {
		if (walkaddr(p->pagetable, a) != 0)
			return -1;
	}

	// Convert prot bits to PTE flags:
	// port bit0 (R) -> PTE_R (bit1), port bit1 (W) -> PTE_W (bit2),
	// port bit2 (X) -> PTE_X (bit3), plus PTE_U and PTE_V
	uint64 perm = PTE_U | PTE_V;
	if (port & 1) // PROT_READ
		perm |= PTE_R;
	if (port & 2) // PROT_WRITE
		perm |= PTE_W;
	if (port & 4) // PROT_EXEC
		perm |= PTE_X;

	// Allocate and map each page
	for (uint64 a = start; a < start + npages * PGSIZE; a += PGSIZE) {
		char *mem = kalloc();
		if (mem == 0) {
			// Out of memory, undo what we've mapped so far
			uvmunmap(p->pagetable, start, (a - start) / PGSIZE, 1);
			return -1;
		}
		memset(mem, 0, PGSIZE);
		if (mappages(p->pagetable, a, PGSIZE, (uint64)mem, perm) != 0) {
			kfree(mem);
			uvmunmap(p->pagetable, start, (a - start) / PGSIZE, 1);
			return -1;
		}
	}
	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
	struct proc *p = curr_proc();

	// Check: start must be page-aligned
	if (start % PGSIZE != 0)
		return -1;
	if (len == 0)
		return -1;

	uint64 npages = (len + PGSIZE - 1) / PGSIZE;

	// Check: all pages must be mapped
	for (uint64 a = start; a < start + npages * PGSIZE; a += PGSIZE) {
		if (walkaddr(p->pagetable, a) == 0)
			return -1;
	}

	// Unmap and free physical pages
	uvmunmap(p->pagetable, start, npages, 1);
	return 0;
}

/*
* LAB1: you may need to define sys_trace here
*/
uint64 sys_trace(uint64 trace_request, uint64 id, uint64 data)
{
	struct proc *p = curr_proc();
	switch (trace_request) {
	case 0: { // read byte at virtual address id
		uint64 pa = walkaddr(p->pagetable, id);
		if (pa == 0)
			return -1;
		pte_t *pte = walk(p->pagetable, id, 0);
		if (pte == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_R) == 0)
			return -1;
		return *(uint8 *)(pa | (id & 0xFFFULL));
	}
	case 1: { // write byte data to virtual address id
		uint64 pa = walkaddr(p->pagetable, id);
		if (pa == 0)
			return -1;
		pte_t *pte = walk(p->pagetable, id, 0);
		if (pte == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
			return -1;
		*(uint8 *)(pa | (id & 0xFFFULL)) = (uint8)data;
		return 0;
	}
	case 2: // query syscall count for syscall id
		if (id >= 500)
			return -1;
		return p->syscall_count[id];
	default:
		return -1;
	}
}

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter here
	*/
	if (id >= 0 && id < 500)
		curr_proc()->syscall_count[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_trace case here
	*/
	case SYS_trace:
		ret = sys_trace(args[0], args[1], args[2]);
		break;
	default:
		errorf("unknown syscall %d", id);
		ret = -1;
		break;
	}
	trapframe->a0 = ret;
}
