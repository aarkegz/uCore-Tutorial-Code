#include "loader.h"
#include "defs.h"
#include "trap.h"

static int app_cur, app_num;
static uint64 *app_info_ptr;
extern char _app_num[], ekernel[];

__attribute__((aligned(4096))) char user_stack[USER_STACK_SIZE];
__attribute__((aligned(4096))) char kernel_stack[KERNEL_STACK_SIZE];

static uint64 user_stack_get_sp()
{
	return (uint64)user_stack + USER_STACK_SIZE;
}

static uint64 kernel_stack_get_sp()
{
	return (uint64)kernel_stack + KERNEL_STACK_SIZE;
}

static struct trap_context *kernel_stack_push_context(struct trap_context cx)
{
	struct trap_context *cx_ptr = (struct trap_context *)(kernel_stack_get_sp() -
							      sizeof(struct trap_context));
	*cx_ptr = cx;
	return cx_ptr;
}

void loader_init()
{
	if ((uint64)ekernel >= BASE_ADDRESS) {
		panic("kernel too large...\n");
	}
	app_info_ptr = (uint64 *)_app_num;
	app_cur = -1;
	app_num = *app_info_ptr;
	infof("[kernel] num_app = %d", app_num);
	for (int i = 0; i < app_num; i++) {
		infof("[kernel] app_%d [%p, %p)", i, app_info_ptr[1 + i],
		      app_info_ptr[2 + i]);
	}
}

static void load_app(int app_id)
{
	if (app_id >= app_num) {
		printf("All applications completed!\n");
		exit_success();
	}
	infof("[kernel] Loading app_%d", app_id);
	app_info_ptr++;
	uint64 start = app_info_ptr[0], end = app_info_ptr[1];
	uint64 length = end - start;
	memset((void *)BASE_ADDRESS, 0, MAX_APP_SIZE);
	memmove((void *)BASE_ADDRESS, (void *)start, length);
	// Memory fence about fetching the instruction memory.
	// It is guaranteed that a subsequent instruction fetch must
	// observe all previous writes to the instruction memory.
	// Therefore, fence.i must be executed after we have loaded
	// the code of the next app into the instruction memory.
	asm volatile("fence.i");
}

static struct trap_context app_init_context(uint64 entry, uint64 sp)
{
	struct trap_context cx;
	memset(&cx, 0, sizeof(cx));
	cx.x[2] = sp;
	uint64 sstatus = r_sstatus();
	sstatus &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
	cx.sstatus = sstatus;
	cx.sepc = entry;
	return cx;
}

void run_next_app()
{
	extern void __restore(struct trap_context *);
	app_cur++;
	load_app(app_cur);
	struct trap_context cx = app_init_context(BASE_ADDRESS,
						  user_stack_get_sp());
	// set sscratch to kernel stack top for trap entry
	w_sscratch(kernel_stack_get_sp());
	__restore(kernel_stack_push_context(cx));
	panic("Unreachable in run_next_app!");
}
