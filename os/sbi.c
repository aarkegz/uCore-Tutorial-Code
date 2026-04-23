#include "sbi.h"
#include "types.h"

const uint64 SBI_SET_TIMER = 0;
const uint64 SBI_CONSOLE_PUTCHAR = 1;
const uint64 SBI_CONSOLE_GETCHAR = 2;
const uint64 SBI_CLEAR_IPI = 3;
const uint64 SBI_SEND_IPI = 4;
const uint64 SBI_REMOTE_FENCE_I = 5;
const uint64 SBI_REMOTE_SFENCE_VMA = 6;
const uint64 SBI_REMOTE_SFENCE_VMA_ASID = 7;
const uint64 SBI_SHUTDOWN = 8;

int inline sbi_call(uint64 which, uint64 arg0, uint64 arg1, uint64 arg2)
{
	register uint64 a0 asm("a0") = arg0;
	register uint64 a1 asm("a1") = arg1;
	register uint64 a2 asm("a2") = arg2;
	register uint64 a7 asm("a7") = which;
	asm volatile("ecall"
		     : "=r"(a0)
		     : "r"(a0), "r"(a1), "r"(a2), "r"(a7)
		     : "memory");
	return a0;
}

void console_putchar(int c)
{
	sbi_call(SBI_CONSOLE_PUTCHAR, c, 0, 0);
}

int console_getchar()
{
	return sbi_call(SBI_CONSOLE_GETCHAR, 0, 0, 0);
}

// QEMU sifive_test device address
#define VIRT_TEST 0x100000
#define EXIT_SUCCESS 0x5555
#define EXIT_FAILURE_FLAG 0x3333
#define EXIT_RESET 0x7777

static void exit_code(uint32 code)
{
	uint32 val;
	if (code == EXIT_SUCCESS || code == EXIT_RESET) {
		val = code;
	} else {
		val = (code << 16) | EXIT_FAILURE_FLAG;
	}
	// Write to sifive_test device to exit QEMU
	*(volatile uint32 *)VIRT_TEST = val;
	// In case exit didn't work, loop forever
	while (1) {
		asm volatile("wfi");
	}
}

void shutdown()
{
	exit_code(EXIT_SUCCESS);
}

void exit_success()
{
	exit_code(EXIT_SUCCESS);
}

void exit_failure()
{
	exit_code(1);
}
