#ifndef SBI_H
#define SBI_H

#include "types.h"

void console_putchar(int);
int console_getchar();
void shutdown();
void exit_success();
void exit_failure();
void set_timer(uint64 stime);

#endif // SBI_H
