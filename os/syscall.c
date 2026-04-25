#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "signal.h"
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
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
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

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	return -1;
}

uint64 sys_set_priority(long long prio)
{
	// TODO: your job is to complete the sys call
	return -1;
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

uint64 sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if (growproc(n) < 0)
		return -1;
	return addr;
}

int sys_fstat(int fd, uint64 stat)
{
	/* Not fully implemented, return -1 as rCore does */
	return -1;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath,
	       uint64 flags)
{
	//TODO: your job is to complete the syscall
	return -1;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
	//TODO: your job is to complete the syscall
	return -1;
}

// TODO: add support for mmap and munmap syscall. (LAB1)
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
uint64 sys_mmap(uint64 start, uint64 len, uint64 port)
{
	// TODO: implement sys_mmap (LAB1)
	return -1;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
	// TODO: implement sys_munmap (LAB1)
	return -1;
}

/*
* LAB1: you may need to define sys_trace here
*/
uint64 sys_trace(uint64 trace_request, uint64 id, uint64 data)
{
	// TODO: implement sys_trace (LAB1)
	return -1;
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
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
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
	case SYS_dup:
		ret = sys_dup(args[0]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], args[1]);
		break;
	case SYS_linkat:
		ret = sys_linkat(args[0], args[1], args[2], args[3], args[4]);
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
		errorf("unknown syscall %d", id);
		ret = -1;
		break;
	}
	trapframe->a0 = ret;
}
