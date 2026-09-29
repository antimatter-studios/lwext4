/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup, newlib system calls and RAM usage accounting.
 */
#ifndef SYSMEM_H_
#define SYSMEM_H_

#include <stdint.h>

/* Paints the free RAM so the stack high water mark can be measured. */
void sysmem_init(void);
uint32_t sysmem_ram_size(void);
/* Prints "MEM: ..." lines: static RAM, heap and stack high water marks. */
void sysmem_report(void);
/* Prints the current and peak heap use after a test phase. */
void sysmem_phase(const char *what);

#endif /* SYSMEM_H_ */
