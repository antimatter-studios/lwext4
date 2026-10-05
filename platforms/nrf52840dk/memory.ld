/* SPDX-License-Identifier: BSD-3-Clause */
/* nRF52840 (no SoftDevice) */
MEMORY
{
	FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 1024K
	RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
__stack_size = 8K;
