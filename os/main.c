#include "console.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"

extern char stext[];
extern char etext[];
extern char srodata[];
extern char erodata[];
extern char sdata[];
extern char edata[];
extern char sbss[];
extern char ebss[];
extern char boot_stack_lower_bound[];
extern char boot_stack_top[];

void clear_bss()
{
	char *p;
	for (p = sbss; p < ebss; ++p)
		*p = 0;
}

void main()
{
	clear_bss();
	printf("hello wrold!\n");
	trap_init();
	loader_init();
	run_next_app();
}
