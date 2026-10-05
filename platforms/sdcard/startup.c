/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Cortex-M startup code, newlib system calls and RAM accounting.
 *
 * Memory layout (see cortex-m.ld): .data, .bss, then the heap growing up
 * from _end towards __heap_limit, and the main stack growing down from
 * _estack. The stack reservation (__stack_size, per board) is what a
 * product would configure; the heap may use everything else.
 */
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "board.h"
#include "sysmem.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _end, _estack;
extern uint32_t __heap_limit, __ram_start, __stack_size;

int main(void);
/* platform.c (the platform API) reports main()'s status, when linked in */
void platform_exit(int status) __attribute__((weak, noreturn));

#define PAINT 0xa5a5a5a5u

static uint8_t *heap_brk = (uint8_t *)&_end;
static uint8_t *heap_max = (uint8_t *)&_end;

/* ------------------------------------------------------------------------ */
/* Fault reporting, written to work without a working C library              */

static void raw_puts(const char *s)
{
	while (*s)
		board_uart_putc(*s++);
}

static void raw_hex(uint32_t v)
{
	static const char hex[] = "0123456789abcdef";
	char buf[11] = "0x";
	for (int i = 0; i < 8; i++)
		buf[2 + i] = hex[(v >> (28 - 4 * i)) & 0xf];
	buf[10] = '\0';
	raw_puts(buf);
}

void fault_report(uint32_t *frame, uint32_t exc_return)
{
	uint32_t ipsr;
	__asm volatile("mrs %0, ipsr" : "=r"(ipsr));
	raw_puts("\nLWEXT4-TEST: FAIL: fault exception ");
	raw_hex(ipsr & 0x1ff);
	raw_puts(" pc=");
	raw_hex(frame[6]);
	raw_puts(" lr=");
	raw_hex(frame[5]);
	raw_puts(" sp=");
	raw_hex((uint32_t)frame);
	raw_puts(" exc_return=");
	raw_hex(exc_return);
#if __ARM_ARCH >= 7
	raw_puts(" cfsr=");
	raw_hex(*(volatile uint32_t *)0xe000ed28);
	raw_puts(" mmfar=");
	raw_hex(*(volatile uint32_t *)0xe000ed34);
	raw_puts(" bfar=");
	raw_hex(*(volatile uint32_t *)0xe000ed38);
#endif
	raw_puts("\n");
	for (;;)
		;
}

__attribute__((naked)) void Fault_Handler(void)
{
	__asm volatile(
		"mov r1, lr\n"
		"movs r0, #4\n"
		"tst r0, r1\n"
		"beq 1f\n"
		"mrs r0, psp\n"
		"b 2f\n"
		"1: mrs r0, msp\n"
		"2: ldr r2, =fault_report\n"
		"bx r2\n");
}

/* ------------------------------------------------------------------------ */
/* Reset                                                                     */

__attribute__((noreturn)) void Reset_Handler(void)
{
	uint32_t *src = &_sidata, *dst = &_sdata;

	while (dst < &_edata)
		*dst++ = *src++;
	for (dst = &_sbss; dst < &_ebss;)
		*dst++ = 0;

#if defined(__ARM_FP)
	/* CP10/CP11 full access for the FPU */
	*(volatile uint32_t *)0xe000ed88 |= 0xfu << 20;
	__asm volatile("dsb\n isb");
#endif
#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
	/* ARMv8-M stack limit: overflowing the reservation faults (STKOF) */
	__asm volatile("msr msplim, %0" ::"r"((uint32_t)&__heap_limit));
#endif
	int status = main();

	if (platform_exit)
		platform_exit(status);
	for (;;)
		;
}

static void Default_Handler(void)
{
	raw_puts("\nLWEXT4-TEST: FAIL: unexpected interrupt\n");
	for (;;)
		;
}

__attribute__((section(".isr_vector"), used)) void (*const vectors[48])(void) = {
	(void (*)(void))&_estack,
	Reset_Handler,
	Fault_Handler, /* NMI */
	Fault_Handler, /* HardFault */
	Fault_Handler, /* MemManage */
	Fault_Handler, /* BusFault */
	Fault_Handler, /* UsageFault */
	Fault_Handler, /* SecureFault */
	0, 0, 0,
	Default_Handler, /* SVC */
	Default_Handler, /* DebugMon */
	0,
	Default_Handler, /* PendSV */
	Default_Handler, /* SysTick */
	/* external interrupts: all masked, never enabled */
	[16 ... 47] = Default_Handler,
};

/* ------------------------------------------------------------------------ */
/* RAM accounting                                                            */

static uint32_t current_sp(void)
{
	uint32_t sp;
	__asm volatile("mov %0, sp" : "=r"(sp));
	return sp;
}

void sysmem_init(void)
{
	uint32_t *p = (uint32_t *)&_end;
	uint32_t *top = (uint32_t *)(current_sp() - 64);

	while (p < top)
		*p++ = PAINT;
}

uint32_t sysmem_ram_size(void)
{
	return (uint32_t)&_estack - (uint32_t)&__ram_start;
}

void sysmem_report(void)
{
	uint32_t *p = (uint32_t *)(((uint32_t)heap_max + 3u) & ~3u);
	uint32_t stack_used, static_ram;

	/* lowest word the stack has written, searching up from the heap top */
	while (p < &_estack && *p == PAINT)
		p++;
	stack_used = (uint32_t)&_estack - (uint32_t)p;
	static_ram = (uint32_t)&_end - (uint32_t)&__ram_start;

	printf("MEM: ram=%lu static=%lu heap_peak=%lu stack_peak=%lu "
	       "stack_reserved=%lu total_peak=%lu\n",
	       (unsigned long)sysmem_ram_size(), (unsigned long)static_ram,
	       (unsigned long)(heap_max - (uint8_t *)&_end),
	       (unsigned long)stack_used, (unsigned long)&__stack_size,
	       (unsigned long)(static_ram + (heap_max - (uint8_t *)&_end) +
			       stack_used));
	if (stack_used > (uint32_t)&__stack_size)
		printf("MEM: WARNING: stack exceeded its reservation\n");
}

void sysmem_phase(const char *what)
{
	struct mallinfo mi = mallinfo();
	printf("MEM: %s: heap in use %lu, heap peak %lu\n", what,
	       (unsigned long)mi.uordblks,
	       (unsigned long)(heap_max - (uint8_t *)&_end));
}

/* ------------------------------------------------------------------------ */
/* newlib system calls                                                       */

void *_sbrk(ptrdiff_t incr)
{
	uint8_t *prev = heap_brk;

	if (heap_brk + incr > (uint8_t *)&__heap_limit) {
		errno = ENOMEM;
		return (void *)-1;
	}
	heap_brk += incr;
	if (heap_brk > heap_max)
		heap_max = heap_brk;
	return prev;
}

int _write(int fd, const char *buf, int len)
{
	(void)fd;
	for (int i = 0; i < len; i++) {
		if (buf[i] == '\n')
			board_uart_putc('\r');
		board_uart_putc(buf[i]);
	}
	return len;
}

int _read(int fd, char *buf, int len)
{
	(void)fd;
	(void)buf;
	(void)len;
	return 0;
}

int _close(int fd)
{
	(void)fd;
	return -1;
}

int _lseek(int fd, int off, int whence)
{
	(void)fd;
	(void)off;
	(void)whence;
	return 0;
}

int _fstat(int fd, struct stat *st)
{
	(void)fd;
	st->st_mode = S_IFCHR;
	return 0;
}

int _isatty(int fd)
{
	(void)fd;
	return 1;
}

void _exit(int code)
{
	(void)code;
	for (;;)
		;
}

int _kill(int pid, int sig)
{
	(void)pid;
	(void)sig;
	errno = EINVAL;
	return -1;
}

int _getpid(void)
{
	return 1;
}
