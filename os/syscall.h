#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

uint64 syscall(uint64 syscall_id, uint64 arg0, uint64 arg1, uint64 arg2);

#endif // SYSCALL_H
