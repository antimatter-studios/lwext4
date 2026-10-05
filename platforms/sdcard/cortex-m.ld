/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Linker script for the Renode test firmware. The board's memory.ld
 * defines the FLASH and RAM regions with the real chip's sizes and the
 * __stack_size reservation.
 */
INCLUDE memory.ld

ENTRY(Reset_Handler)

SECTIONS
{
	.text :
	{
		KEEP(*(.isr_vector))
		*(.text*)
		*(.rodata*)
		. = ALIGN(4);
	} > FLASH

	.ARM.exidx :
	{
		*(.ARM.exidx*)
	} > FLASH

	_sidata = LOADADDR(.data);

	.data :
	{
		__ram_start = ADDR(.data);
		_sdata = .;
		*(.data*)
		. = ALIGN(4);
		_edata = .;
	} > RAM AT > FLASH

	.bss (NOLOAD) :
	{
		_sbss = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(8);
		_ebss = .;
	} > RAM

	_end = .;
	end = .;
	_estack = ORIGIN(RAM) + LENGTH(RAM);
	__heap_limit = _estack - __stack_size;

	ASSERT(_end <= __heap_limit, "static RAM + stack reservation exceed RAM")
}
