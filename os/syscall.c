#include "console.h"
#include "defs.h"
#include "loader.h"
#include "signal.h"
#include "sync.h"
#include "syscall.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_PIPE:
		return pipewrite(f->pipe, va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_PIPE:
		return piperead(f->pipe, va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_pipe(uint64 fdarray)
{
	struct proc *p = curr_proc();
	uint64 fd0, fd1;
	struct file *f0, *f1;
	if (f0 < 0 || f1 < 0) {
		return -1;
	}
	f0 = filealloc();
	f1 = filealloc();
	if (pipealloc(f0, f1) < 0)
		goto err0;
	fd0 = fdalloc(f0);
	fd1 = fdalloc(f1);
	if (fd0 < 0 || fd1 < 0)
		goto err0;
	if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
	    copyout(p->pagetable, fdarray + sizeof(uint64), (char *)&fd1,
		    sizeof(fd1)) < 0) {
		goto err1;
	}
	return 0;

err1:
	p->files[fd0] = 0;
	p->files[fd1] = 0;
err0:
	fileclose(f0);
	fileclose(f1);
	return -1;
}

uint64 sys_dup(int fd)
{
	if (fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		return -1;
	}
	/* Find the smallest available fd and duplicate the file */
	int new_fd = -1;
	for (int i = 0; i < FD_BUFFER_SIZE; i++) {
		if (p->files[i] == NULL) {
			new_fd = i;
			break;
		}
	}
	if (new_fd < 0) {
		return -1;
	}
	f->ref++;
	p->files[new_fd] = f;
	return new_fd;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_thread_create(uint64 entry, uint64 arg)
{
	struct proc *p = curr_proc();
	int tid = allocthread(p, entry, 1);
	if (tid < 0) {
		errorf("fail to create thread");
		return -1;
	}
	struct thread *t = &p->threads[tid];
	t->trapframe->a0 = arg;
	t->state = RUNNABLE;
	add_task(t);
	return tid;
}

int sys_fstat(int fd, uint64 stat)
{
	struct proc *p = curr_proc();
	if (fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;
	struct file *f = p->files[fd];
	if (f == NULL || f->type != FD_INODE)
		return -1;
	struct inode *ip = f->ip;
	ivalid(ip);
	struct Stat st;
	st.dev = ip->dev;
	st.ino = ip->inum;
	if (ip->type == T_DIR)
		st.mode = STAT_MODE_DIR;
	else if (ip->type == T_FILE)
		st.mode = STAT_MODE_FILE;
	else
		st.mode = STAT_MODE_NULL;
	st.nlink = ip->nlink;
	memset(st.pad, 0, sizeof(st.pad));
	if (copyout(p->pagetable, stat, (char *)&st, sizeof(st)) < 0)
		return -1;
	return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath,
	       uint64 flags)
{
	struct proc *p = curr_proc();
	char old_name[MAXPATH], new_name[MAXPATH];
	copyinstr(p->pagetable, old_name, oldpath, MAXPATH);
	copyinstr(p->pagetable, new_name, newpath, MAXPATH);

	struct inode *dp = root_dir();
	ivalid(dp);
	struct inode *ip = dirlookup(dp, old_name, 0);
	if (ip == 0) {
		iput(dp);
		return -1;
	}
	ivalid(ip);
	if (ip->type == T_DIR) {
		iput(ip);
		iput(dp);
		return -1;
	}
	// Cannot link to self
	if (strncmp(old_name, new_name, DIRSIZ) == 0) {
		iput(ip);
		iput(dp);
		return -1;
	}
	ip->nlink++;
	iupdate(ip);
	if (dirlink(dp, new_name, ip->inum) < 0) {
		ip->nlink--;
		iupdate(ip);
		iput(ip);
		iput(dp);
		return -1;
	}
	iput(ip);
	iput(dp);
	return 0;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
	struct proc *p = curr_proc();
	char path[MAXPATH];
	copyinstr(p->pagetable, path, name, MAXPATH);

	struct inode *dp = root_dir();
	ivalid(dp);
	struct inode *ip = dirlookup(dp, path, 0);
	if (ip == 0) {
		iput(dp);
		return -1;
	}
	ivalid(ip);
	if (ip->type == T_DIR) {
		iput(ip);
		iput(dp);
		return -1;
	}
	if (dirunlink(dp, path, ip->inum) < 0) {
		iput(ip);
		iput(dp);
		return -1;
	}
	ip->nlink--;
	iupdate(ip);
	iput(ip);
	iput(dp);
	return 0;
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if (growproc(n) < 0)
		return -1;
	return addr;
}

int sys_gettid()
{
	return curr_thread()->tid;
}

int sys_waittid(int tid)
{
	if (tid < 0 || tid >= NTHREAD) {
		errorf("unexpected tid %d", tid);
		return -1;
	}
	struct thread *t = &curr_proc()->threads[tid];
	if (t->state == T_UNUSED || tid == curr_thread()->tid) {
		return -1;
	}
	if (t->state != EXITED) {
		return -2;
	}
	memset((void *)t->kstack, 7, KSTACK_SIZE);
	t->tid = -1;
	t->state = T_UNUSED;
	return t->exit_code;
}

/*
*	LAB5: (3) Banker's Algorithm for deadlock detection.
*      Resources: 0..LOCK_POOL_SIZE-1 = mutexes, LOCK_POOL_SIZE..2*LOCK_POOL_SIZE-1 = semaphores
*/
#define DEADLOCK_RES_SIZE (2 * LOCK_POOL_SIZE)

static int deadlock_detect(struct proc *p)
{
	int work[DEADLOCK_RES_SIZE];
	int finish[NTHREAD];

	for (int j = 0; j < DEADLOCK_RES_SIZE; j++)
		work[j] = p->available[j];

	for (int i = 0; i < NTHREAD; i++) {
		finish[i] = 1;
		for (int j = 0; j < DEADLOCK_RES_SIZE; j++) {
			if (p->allocation[i][j] > 0) {
				finish[i] = 0;
				break;
			}
		}
	}

	int found;
	do {
		found = 0;
		for (int i = 0; i < NTHREAD; i++) {
			if (finish[i])
				continue;
			int can_satisfy = 1;
			for (int j = 0; j < DEADLOCK_RES_SIZE; j++) {
				if (p->request[i][j] > work[j]) {
					can_satisfy = 0;
					break;
				}
			}
			if (can_satisfy) {
				for (int j = 0; j < DEADLOCK_RES_SIZE; j++)
					work[j] += p->allocation[i][j];
				finish[i] = 1;
				found = 1;
			}
		}
	} while (found);

	for (int i = 0; i < NTHREAD; i++) {
		if (!finish[i])
			return 1; // deadlock
	}
	return 0; // safe
}

int sys_mutex_create(int blocking)
{
	struct proc *p = curr_proc();
	struct mutex *m = mutex_create(blocking);
	if (m == NULL) {
		errorf("fail to create mutex: out of resource");
		return -1;
	}
	int mutex_id = m - p->mutex_pool;
	if (p->deadlock_detect_enabled) {
		p->available[mutex_id] = 1; // mutex has 1 available instance
	}
	debugf("create mutex %d", mutex_id);
	return mutex_id;
}

int sys_mutex_lock(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
		errorf("Unexpected mutex id %d", mutex_id);
		return -1;
	}
	struct proc *p = curr_proc();
	int tid = curr_thread()->tid;
	if (p->deadlock_detect_enabled) {
		p->request[tid][mutex_id] = 1;
		if (deadlock_detect(p)) {
			p->request[tid][mutex_id] = 0;
			return -0xDEAD;
		}
	}
	mutex_lock(&p->mutex_pool[mutex_id]);
	if (p->deadlock_detect_enabled) {
		p->allocation[tid][mutex_id] = 1;
		p->available[mutex_id] = 0;
		p->request[tid][mutex_id] = 0;
	}
	return 0;
}

int sys_mutex_unlock(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
		errorf("Unexpected mutex id %d", mutex_id);
		return -1;
	}
	struct proc *p = curr_proc();
	int tid = curr_thread()->tid;
	if (p->deadlock_detect_enabled) {
		p->allocation[tid][mutex_id] = 0;
		p->available[mutex_id] = 1;
	}
	mutex_unlock(&p->mutex_pool[mutex_id]);
	return 0;
}

int sys_semaphore_create(int res_count)
{
	struct proc *p = curr_proc();
	struct semaphore *s = semaphore_create(res_count);
	if (s == NULL) {
		errorf("fail to create semaphore: out of resource");
		return -1;
	}
	int sem_id = s - p->semaphore_pool;
	if (p->deadlock_detect_enabled) {
		p->available[LOCK_POOL_SIZE + sem_id] = res_count;
	}
	debugf("create semaphore %d", sem_id);
	return sem_id;
}

int sys_semaphore_up(int semaphore_id)
{
	if (semaphore_id < 0 ||
	    semaphore_id >= curr_proc()->next_semaphore_id) {
		errorf("Unexpected semaphore id %d", semaphore_id);
		return -1;
	}
	struct proc *p = curr_proc();
	int tid = curr_thread()->tid;
	int res_id = LOCK_POOL_SIZE + semaphore_id;
	if (p->deadlock_detect_enabled) {
		p->allocation[tid][res_id]--;
		p->available[res_id]++;
	}
	semaphore_up(&p->semaphore_pool[semaphore_id]);
	return 0;
}

int sys_semaphore_down(int semaphore_id)
{
	if (semaphore_id < 0 ||
	    semaphore_id >= curr_proc()->next_semaphore_id) {
		errorf("Unexpected semaphore id %d", semaphore_id);
		return -1;
	}
	struct proc *p = curr_proc();
	int tid = curr_thread()->tid;
	int res_id = LOCK_POOL_SIZE + semaphore_id;
	if (p->deadlock_detect_enabled) {
		struct semaphore *s = &p->semaphore_pool[semaphore_id];
		if (s->count <= 0) {
			// Would block — check deadlock before proceeding
			p->request[tid][res_id] = 1;
			if (deadlock_detect(p)) {
				p->request[tid][res_id] = 0;
				return -0xDEAD;
			}
			// Safe to block
			semaphore_down(s);
			// Woken up — acquired the resource
			p->allocation[tid][res_id]++;
			p->available[res_id]--;
			p->request[tid][res_id] = 0;
		} else {
			// Can get immediately, no deadlock concern
			semaphore_down(s);
			p->allocation[tid][res_id]++;
			p->available[res_id]--;
		}
	} else {
		semaphore_down(&curr_proc()->semaphore_pool[semaphore_id]);
	}
	return 0;
}

int sys_condvar_create()
{
	struct condvar *c = condvar_create();
	if (c == NULL) {
		errorf("fail to create condvar: out of resource");
		return -1;
	}
	int cond_id = c - curr_proc()->condvar_pool;
	debugf("create condvar %d", cond_id);
	return cond_id;
}

int sys_condvar_signal(int cond_id)
{
	if (cond_id < 0 || cond_id >= curr_proc()->next_condvar_id) {
		errorf("Unexpected condvar id %d", cond_id);
		return -1;
	}
	cond_signal(&curr_proc()->condvar_pool[cond_id]);
	return 0;
}

int sys_condvar_wait(int cond_id, int mutex_id)
{
	if (cond_id < 0 || cond_id >= curr_proc()->next_condvar_id) {
		errorf("Unexpected condvar id %d", cond_id);
		return -1;
	}
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
		errorf("Unexpected mutex id %d", mutex_id);
		return -1;
	}
	cond_wait(&curr_proc()->condvar_pool[cond_id],
		  &curr_proc()->mutex_pool[mutex_id]);
	return 0;
}

int sys_enable_deadlock_detect(int is_enable)
{
	curr_proc()->deadlock_detect_enabled = is_enable;
	return 0;
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

extern char trap_page[];

uint64 sys_kill(int pid, int signum)
{
	struct proc *p = pid2proc(pid);
	if (p == NULL) {
		return -1;
	}
	if (signum < 0 || signum > MAX_SIG) {
		return -1;
	}
	uint32 signal = 1U << signum;
	if (p->signals & signal) {
		return -1;
	}
	p->signals |= signal;
	return 0;
}

uint64 sys_sigprocmask(uint32 mask)
{
	struct proc *p = curr_proc();
	uint32 old_mask = p->signal_mask;
	p->signal_mask = mask;
	return old_mask;
}

uint64 sys_sigreturn(void)
{
	struct proc *p = curr_proc();
	p->handling_sig = -1;
	/* Restore the trap context from backup */
	if (p->trap_ctx_backup) {
		*p->trapframe = *p->trap_ctx_backup;
	}
	/* Return the value of a0 in the trap context */
	return p->trapframe->a0;
}

static int check_sigaction_error(uint32 signal, uint64 action, uint64 old_action)
{
	if (action == 0 || old_action == 0 || signal == SIGKILL ||
	    signal == SIGSTOP) {
		return 1;
	}
	return 0;
}

uint64 sys_sigaction(int signum, uint64 action, uint64 old_action)
{
	struct proc *p = curr_proc();
	if (signum < 0 || signum > MAX_SIG) {
		return -1;
	}
	uint32 signal = 1U << signum;
	if (check_sigaction_error(signal, action, old_action)) {
		return -1;
	}
	/* Save old action to user space */
	struct SignalAction prev = p->signal_actions.table[signum];
	copyout(p->pagetable, old_action, (char *)&prev,
		sizeof(struct SignalAction));
	/* Read new action from user space */
	struct SignalAction new_action;
	copyin(p->pagetable, (char *)&new_action, action,
	       sizeof(struct SignalAction));
	p->signal_actions.table[signum] = new_action;
	return 0;
}

void syscall()
{
	struct trapframe *trapframe = curr_thread()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	if (id != SYS_write && id != SYS_read && id != SYS_sched_yield) {
		debugf("syscall %d args = [%x, %x, %x, %x, %x, %x]", id,
		       args[0], args[1], args[2], args[3], args[4], args[5]);
	}
	if (id >= 0 && id < 500)
		curr_proc()->syscall_count[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	// case SYS_nanosleep:
	// 	ret = sys_nanosleep(args[0]);
	// 	break;
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
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_pipe2:
		ret = sys_pipe(args[0]);
		break;
	case SYS_thread_create:
		ret = sys_thread_create(args[0], args[1]);
		break;
	case SYS_dup:
		ret = sys_dup(args[0]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], args[1]);
		break;
	case SYS_gettid:
		ret = sys_gettid();
		break;
	case SYS_waittid:
		ret = sys_waittid(args[0]);
		break;
	case SYS_mutex_create:
		ret = sys_mutex_create(args[0]);
		break;
	case SYS_mutex_lock:
		ret = sys_mutex_lock(args[0]);
		break;
	case SYS_mutex_unlock:
		ret = sys_mutex_unlock(args[0]);
		break;
	case SYS_semaphore_create:
		ret = sys_semaphore_create(args[0]);
		break;
	case SYS_semaphore_up:
		ret = sys_semaphore_up(args[0]);
		break;
	case SYS_semaphore_down:
		ret = sys_semaphore_down(args[0]);
		break;
	case SYS_condvar_create:
		ret = sys_condvar_create();
		break;
	case SYS_condvar_signal:
		ret = sys_condvar_signal(args[0]);
		break;
	case SYS_condvar_wait:
		ret = sys_condvar_wait(args[0], args[1]);
		break;
	case SYS_enable_deadlock_detect:
		ret = sys_enable_deadlock_detect(args[0]);
		break;
	case SYS_unlinkat:
		ret = sys_unlinkat(args[0], args[1], args[2]);
		break;
	case SYS_kill:
		ret = sys_kill(args[0], args[1]);
		break;
	case SYS_rt_sigaction:
		ret = sys_sigaction(args[0], args[1], args[2]);
		break;
	case SYS_rt_sigprocmask:
		ret = sys_sigprocmask(args[0]);
		break;
	case SYS_rt_sigreturn:
		ret = sys_sigreturn();
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
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	curr_thread()->trapframe->a0 = ret;
	if (id != SYS_write && id != SYS_read && id != SYS_sched_yield) {
		debugf("syscall %d ret %d", id, ret);
	}
}
