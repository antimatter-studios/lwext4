/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * What firmware needs from the board it runs on: the API every
 * platforms/<board>/ implements (see README.md there). tests/baremetal,
 * tests/bench and the example applications are written against it, so the
 * code an application starts from is the code the tests run.
 */
#ifndef LWEXT4_PLATFORM_H_
#define LWEXT4_PLATFORM_H_

#include <stdint.h>

/**@brief Set up the console: stdout works after it.*/
void platform_init(void);

/**@brief Stop the machine: 0 success, anything else failure. Under an
 * emulator this ends the run with that status.*/
void platform_exit(int status) __attribute__((noreturn));

/**@brief A counter that only goes up, to measure with, in
 * platform_counter_unit: instructions on the MPS2 boards under QEMU.*/
uint64_t platform_counter(void);
extern const char *const platform_counter_unit;

#endif
