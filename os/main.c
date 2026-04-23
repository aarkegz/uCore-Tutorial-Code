#include "console.h"
#include "defs.h"
#include "loader.h"
#include "plic.h"
#include "timer.h"
#include "trap.h"
#include "virtio.h"

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
	printf("[kernel] Hello, world!\n");
	tracef("[kernel] .text [%p, %p)", stext, etext);
	debugf("[kernel] .rodata [%p, %p)", srodata, erodata);
	infof("[kernel] .data [%p, %p)", sdata, edata);
	warnf("[kernel] boot_stack top=bottom=%p, lower_bound=%p",
	      boot_stack_top, boot_stack_lower_bound);
	errorf("[kernel] .bss [%p, %p)", sbss, ebss);
	proc_init();
	kinit();
	kvm_init();
	trap_init();
	plicinit();
	virtio_disk_init();
	binit();
	fsinit();
	timer_init();
	load_init_app();
	infof("start scheduler!");
	show_all_files();
	scheduler();
}