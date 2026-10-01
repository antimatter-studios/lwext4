/* SPDX-License-Identifier: BSD-3-Clause */
/* STM32F401RE */
MEMORY
{
	FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
	RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}
__stack_size = 8K;
