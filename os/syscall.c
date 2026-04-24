#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"

uint64 sys_write(int fd, char *str, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, str, len);
	if (fd != STDOUT)
		return -1;
	for (int i = 0; i < len; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	debugf("sysexit(%d)", code);
	run_next_app();
	__builtin_unreachable();
}

uint64 syscall(uint64 syscall_id, uint64 arg0, uint64 arg1, uint64 arg2)
{
	tracef("syscall %d args = [%x, %x, %x]", syscall_id, arg0, arg1,
	       arg2);
	switch (syscall_id) {
	case SYS_write:
		return sys_write(arg0, (char *)arg1, arg2);
	case SYS_exit:
		sys_exit(arg0);
	default:
		errorf("unknown syscall %d", syscall_id);
		return -1;
	}
}
