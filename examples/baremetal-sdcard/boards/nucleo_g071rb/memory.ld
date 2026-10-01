/* SPDX-License-Identifier: BSD-3-Clause */
/* STM32G071RB */
MEMORY
{
	FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
	RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 36K
}
__stack_size = 4K;
