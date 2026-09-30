/* SPDX-License-Identifier: BSD-3-Clause */
/* STM32L552ZE, TrustZone disabled: SRAM1 + SRAM2 are contiguous */
MEMORY
{
	FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
	RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
__stack_size = 8K;
