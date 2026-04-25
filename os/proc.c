#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"
#include "queue.h"

struct proc pool[NPROC];
__attribute__((aligned(16))) char kstack[NPROC][NTHREAD][KSTACK_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][NTHREAD][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
struct thread *current_thread;
struct thread idle;
struct proc *init_proc;
struct queue task_queue;

// Recyclable PID allocator
static int recycled_pids[NPROC];
static int recycled_count = 0;
static int next_pid = 1;

int threadid()
{
	return curr_thread()->tid;
}

int cpuid()
{
	return 0;
}

int procid()
{
	return curr_proc()->pid;
}

struct proc *curr_proc()
{
	return current_thread->process;
}

struct thread *curr_thread()
{
	return current_thread;
}

// initialize the proc table at boot time.
void proc_init()
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		p->state = P_UNUSED;
		for (int tid = 0; tid < NTHREAD; ++tid) {
			struct thread *t = &p->threads[tid];
			t->state = T_UNUSED;
		}
		memset(p->syscall_count, 0, sizeof(p->syscall_count));
		p->priority = 16;
		p->stride = 0;
	}
	idle.kstack = (uint64)boot_stack_top;
	current_thread = &idle;
	// for procid() and threadid()
	idle.process = pool;
	idle.tid = -1;
	init_queue(&task_queue, QUEUE_SIZE, process_queue_data);
}

int allocpid()
{
	if (recycled_count > 0) {
		return recycled_pids[--recycled_count];
	}
	return next_pid++;
}

void freepid(int pid)
{
	if (pid > 0 && recycled_count < NPROC) {
		recycled_pids[recycled_count++] = pid;
	}
}

int alloctid(const struct proc *process)
{
	for (int i = 0; i < NTHREAD; ++i) {
		if (process->threads[i].state == T_UNUSED)
			return i;
	}
	return -1;
}

// get task by unique task id
struct thread *id_to_task(int index)
{
	if (index < 0) {
		return NULL;
	}
	int pool_id = index / NTHREAD;
	int tid = index % NTHREAD;
	struct thread *t = &pool[pool_id].threads[tid];
	return t;
}

// ncode unique task id for each thread
int task_to_id(struct thread *t)
{
	int pool_id = t->process - pool;
	int task_id = pool_id * NTHREAD + t->tid;
	return task_id;
}

struct thread *fetch_task()
{
	int index = pop_queue(&task_queue);
	struct thread *t = id_to_task(index);
	if (t == NULL) {
		debugf("No task to fetch\n");
		return t;
	}
	int tid = t->tid;
	int pid = t->process->pid;
	tracef("fetch index %d(pid=%d, tid=%d, addr=%p) from task queue", index,
	       pid, tid, (uint64)t);
	return t;
}

void add_task(struct thread *t)
{
	int task_id = task_to_id(t);
	int pid = t->process->pid;
	push_queue(&task_queue, task_id);
	tracef("add index %d(pid=%d, tid=%d, addr=%p) to task queue", task_id,
	       pid, t->tid, (uint64)t);
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *allocproc()
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == P_UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	// init proc
	p->pid = allocpid();
	p->state = P_USED;
	p->max_page = 0;
	p->parent = NULL;
	p->exit_code = 0;
	p->pagetable = uvmcreate();
	memset((void *)p->files, 0, sizeof(struct file *) * FD_BUFFER_SIZE);
	p->next_mutex_id = 0;
	p->next_semaphore_id = 0;
	p->next_condvar_id = 0;
	p->program_brk = 0;
	p->heap_bottom = 0;
	/* Initialize signal fields */
	p->signals = 0;
	p->signal_mask = 0;
	p->handling_sig = -1;
	signal_actions_init(&p->signal_actions);
	p->killed = 0;
	p->frozen = 0;
	p->trap_ctx_backup = NULL;
	memset(p->syscall_count, 0, sizeof(p->syscall_count));
	p->priority = 16;
	p->stride = 0;
	p->deadlock_detect_enabled = 0;
	memset(p->available, 0, sizeof(p->available));
	memset(p->allocation, 0, sizeof(p->allocation));
	memset(p->request, 0, sizeof(p->request));
	return p;
}

inline uint64 get_thread_trapframe_va(int tid)
{
	return TRAPFRAME - tid * TRAP_PAGE_SIZE;
}

inline uint64 get_thread_ustack_base_va(struct thread *t)
{
	return t->process->ustack_base + t->tid * USTACK_SIZE;
}

int allocthread(struct proc *p, uint64 entry, int alloc_user_res)
{
	int tid;
	struct thread *t;
	for (tid = 0; tid < NTHREAD; ++tid) {
		t = &p->threads[tid];
		if (t->state == T_UNUSED) {
			goto found;
		}
	}
	return -1;

found:
	t->tid = tid;
	t->state = T_USED;
	t->process = p;
	t->exit_code = 0;
	// kernel stack
	t->kstack = (uint64)kstack[p - pool][tid];
	// don't clear kstack now for exec()
	// memset((void *)t->kstack, 0, KSTACK_SIZE);
	// user stack
	t->ustack = get_thread_ustack_base_va(t);
	if (alloc_user_res != 0) {
		if (uvmmap(p->pagetable, t->ustack, USTACK_SIZE / PAGE_SIZE,
			   PTE_U | PTE_R | PTE_W) < 0) {
			panic("map ustack fail");
		}
		p->max_page =
			MAX(p->max_page,
			    PGROUNDUP(t->ustack + USTACK_SIZE - 1) / PAGE_SIZE);
	}
	// trap frame
	t->trapframe = (struct trapframe *)trapframe[p - pool][tid];
	memset((void *)t->trapframe, 0, TRAP_PAGE_SIZE);
	if (mappages(p->pagetable, get_thread_trapframe_va(tid), TRAP_PAGE_SIZE,
		     (uint64)t->trapframe, PTE_R | PTE_W) < 0) {
		panic("map trapframe fail");
	}
	t->trapframe->sp = t->ustack + USTACK_SIZE;
	t->trapframe->epc = entry;
	//task context
	memset(&t->context, 0, sizeof(t->context));
	t->context.ra = (uint64)usertrapret;
	t->context.sp = t->kstack + KSTACK_SIZE;
	// we do not add thread to scheduler immediately
	debugf("allocthread p: %d, o: %d, t: %d, e: %p, sp: %p, spp: %p",
	       p->pid, (p - pool), t->tid, entry, t->ustack,
	       useraddr(p->pagetable, t->ustack));
	return tid;
}

int init_stdio(struct proc *p)
{
	for (int i = 0; i < 3; i++) {
		if (p->files[i] != NULL) {
			return -1;
		}
		p->files[i] = stdio_init(i);
	}
	return 0;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler()
{
	struct thread *t;
	for (;;) {
		t = fetch_task();
		if (t == NULL) {
			panic("all app are over!\n");
		}
		// throw out freed threads
		if (t->state != RUNNABLE) {
			warnf("not RUNNABLE");
			continue;
		}
		tracef("swtich to proc %d, thread %d", t->process->pid, t->tid);
		t->state = RUNNING;
		current_thread = t;
		swtch(&idle.context, &t->context);
	}
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched()
{
	struct thread *t = curr_thread();
	if (t->state == RUNNING)
		panic("sched running");
	swtch(&t->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void yield()
{
	current_thread->state = RUNNABLE;
	add_task(current_thread);
	sched();
}

// Free a process's page table, and free the
// physical memory it refers to.
void freepagetable(pagetable_t pagetable, uint64 max_page)
{
	uvmunmap(pagetable, TRAMPOLINE, 1, 0);
	uvmunmap(pagetable, TRAPFRAME, 1, 0);
	uvmunmap(pagetable, 0, max_page, 1);
	freewalk_all(pagetable);
}

void freethread(struct thread *t)
{
	pagetable_t pt = t->process->pagetable;
	// fill with junk
	memset((void *)t->trapframe, 6, TRAP_PAGE_SIZE);
	memset(&t->context, 6, sizeof(t->context));
	uvmunmap(pt, get_thread_trapframe_va(t->tid), 1, 0);
	uvmunmap(pt, get_thread_ustack_base_va(t), USTACK_SIZE / PAGE_SIZE, 1);
}

void freeproc(struct proc *p)
{
	for (int tid = 0; tid < NTHREAD; ++tid) {
		struct thread *t = &p->threads[tid];
		if (t->state != T_UNUSED && t->state != EXITED) {
			freethread(t);
		}
		t->state = T_UNUSED;
	}
	if (p->pagetable)
		freepagetable(p->pagetable, p->max_page);
	p->pagetable = 0;
	p->max_page = 0;
	p->ustack_base = 0;
	for (int i = 0; i < FD_BUFFER_SIZE; i++) {
		if (p->files[i] != NULL) {
			fileclose(p->files[i]);
		}
	}
	if (p->trap_ctx_backup) {
		kfree((char *)p->trap_ctx_backup);
		p->trap_ctx_backup = NULL;
	}
	p->state = P_UNUSED;
}

int fork()
{
	struct proc *np;
	struct proc *p = curr_proc();
	int i;
	// Allocate process.
	if ((np = allocproc()) == 0) {
		panic("allocproc\n");
	}
	// Copy user memory from parent to child.
	if (uvmcopy(p->pagetable, np->pagetable, p->max_page) < 0) {
		panic("uvmcopy\n");
	}
	np->max_page = p->max_page;
	np->ustack_base = p->ustack_base;
	// Copy file table to new proc
	for (i = 0; i < FD_BUFFER_SIZE; i++) {
		if (p->files[i] != NULL) {
			// TODO: f->type == STDIO ?
			p->files[i]->ref++;
			np->files[i] = p->files[i];
		}
	}

	np->parent = p;
	np->program_brk = p->program_brk;
	np->heap_bottom = p->heap_bottom;
	/* Inherit signal_mask and signal_actions */
	np->signal_mask = p->signal_mask;
	np->signal_actions = p->signal_actions;
	// currently only copy main thread
	struct thread *nt = &np->threads[allocthread(np, 0, 0)],
		      *t = &p->threads[0];
	// copy saved user registers.
	*(nt->trapframe) = *(t->trapframe);
	// Cause fork to return 0 in the child.
	nt->trapframe->a0 = 0;
	nt->state = RUNNABLE;
	add_task(nt);
	return np->pid;
}

int push_argv(struct proc *p, char **argv)
{
	uint64 argc, ustack[MAX_ARG_NUM + 1];
	// only push to main thread
	struct thread *t = &p->threads[0];
	uint64 sp = t->ustack + USTACK_SIZE, spb = t->ustack;
	debugf("[push] sp: %p, spb: %p", sp, spb);
	// Push argument strings, prepare rest of stack in ustack.
	for (argc = 0; argv[argc]; argc++) {
		if (argc >= MAX_ARG_NUM)
			panic("too many args!");
		sp -= strlen(argv[argc]) + 1;
		sp -= sp % 16; // riscv sp must be 16-byte aligned
		if (sp < spb) {
			panic("uset stack overflow!");
		}
		if (copyout(p->pagetable, sp, argv[argc],
			    strlen(argv[argc]) + 1) < 0) {
			panic("copy argv failed!");
		}
		ustack[argc] = sp;
	}
	ustack[argc] = 0;
	// push the array of argv[] pointers.
	sp -= (argc + 1) * sizeof(uint64);
	sp -= sp % 16;
	if (sp < spb) {
		panic("uset stack overflow!");
	}
	if (copyout(p->pagetable, sp, (char *)ustack,
		    (argc + 1) * sizeof(uint64)) < 0) {
		panic("copy argc failed!");
	}
	t->trapframe->a1 = sp;
	t->trapframe->sp = sp;
	// clear files ?
	return argc; // this ends up in a0, the first argument to main(argc, argv)
}

int exec(char *path, char **argv)
{
	infof("exec : %s\n", path);
	struct inode *ip;
	struct proc *p = curr_proc();
	if ((ip = namei(path)) == 0) {
		errorf("invalid file name %s\n", path);
		return -1;
	}
	// free current main thread's ustack and trapframe
	struct thread *t = curr_thread();
	freethread(t);
	t->state = T_UNUSED;
	uvmunmap(p->pagetable, 0, p->max_page, 1);
	bin_loader(ip, p);
	iput(ip);
	t->state = RUNNING;
	return push_argv(p, argv);
}

int wait(int pid, int *code)
{
	struct proc *np;
	int havekids;
	struct proc *p = curr_proc();
	struct thread *t = curr_thread();

	for (;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for (np = pool; np < &pool[NPROC]; np++) {
			if (np->state != P_UNUSED && np->parent == p &&
			    (pid <= 0 || np->pid == pid)) {
				havekids = 1;
				if (np->state == ZOMBIE) {
					// Found one.
					int recycled_pid = np->pid;
					np->state = P_UNUSED;
					freepid(recycled_pid);
					*code = np->exit_code;
					memset((void *)np->threads[0].kstack, 9,
					       KSTACK_SIZE);
					return recycled_pid;
				}
			}
		}
		if (!havekids) {
			return -1;
		}
		t->state = RUNNABLE;
		add_task(t);
		sched();
	}
}

// Exit the current process.
void exit(int code)
{
	struct proc *p = curr_proc();
	struct thread *t = curr_thread();
	t->exit_code = code;
	t->state = EXITED;
	int tid = t->tid;
	debugf("thread exit with %d", code);
	freethread(t);
	if (tid == 0) {
		p->exit_code = code;
		freeproc(p);
		debugf("proc exit");
		if (p->parent != NULL) {
			// Parent should `wait`
			p->state = ZOMBIE;
		} else {
			// No parent, recycle PID immediately
			freepid(p->pid);
		}
		// Reparent children to init_proc (usershell)
		struct proc *np;
		for (np = pool; np < &pool[NPROC]; np++) {
			if (np->parent == p) {
				np->parent = init_proc;
			}
		}
	}
	sched();
}

int fdalloc(struct file *f)
{
	debugf("debugf f = %p, type = %d", f, f->type);
	struct proc *p = curr_proc();
	for (int i = 0; i < FD_BUFFER_SIZE; ++i) {
		if (p->files[i] == NULL) {
			p->files[i] = f;
			debugf("debugf fd = %d, f = %p", i, p->files[i]);
			return i;
		}
	}
	return -1;
}

struct proc *pid2proc(int pid)
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state != P_UNUSED && p->pid == pid) {
			return p;
		}
	}
	return NULL;
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
	uint64 program_brk;
	struct proc *p = curr_proc();
	program_brk = p->program_brk;
	int new_brk = program_brk + n - p->heap_bottom;
	if (new_brk < 0) {
		return -1;
	}
	if (n > 0) {
		uint64 va;
		for (va = PGROUNDUP(program_brk); va < program_brk + n;
		     va += PGSIZE) {
			char *mem = kalloc();
			if (mem == 0)
				return -1;
			memset(mem, 0, PGSIZE);
			if (mappages(p->pagetable, va, PGSIZE, (uint64)mem,
				     PTE_U | PTE_R | PTE_W) != 0) {
				kfree(mem);
				return -1;
			}
		}
		program_brk += n;
	} else if (n < 0) {
		uvmunmap(p->pagetable, PGROUNDUP(program_brk + n),
			 (PGROUNDUP(program_brk) -
			  PGROUNDUP(program_brk + n)) /
				 PGSIZE,
			 1);
		program_brk += n;
	}
	p->program_brk = program_brk;
	return 0;
}
