#ifndef LOADER_H
#define LOADER_H

#include "const.h"
#include "types.h"

void loader_init();
void run_next_app();

#define BASE_ADDRESS (0x80400000)
#define MAX_APP_SIZE (0x20000)
#define USER_STACK_SIZE (4096 * 2)
#define KERNEL_STACK_SIZE (4096 * 2)

#endif // LOADER_H
