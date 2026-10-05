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

/**@brief The command line the machine was started with ("" if none): on
 * MPS2 the arg= values of QEMU's -semihosting-config, separated by
 * spaces.*/
const char *platform_cmdline(void);

struct ext4_blockdev;

/**@brief The board's storage, as a block device for ext4_mkfs() and
 * ext4_device_register(); NULL if there is none.
 *
 * MPS2: a disk image file on the host, through semihosting: the first word
 * of the command line without '=' (default disk.img), which must exist; its
 * size is the disk's. With cut=<n> on the command line the power is cut
 * at the block write after the n-th: that write is not done, "POWER CUT"
 * is printed and the machine stops with platform_exit(1), which is what a
 * power loss leaves on the disk.*/
struct ext4_blockdev *platform_disk(void);

#endif
