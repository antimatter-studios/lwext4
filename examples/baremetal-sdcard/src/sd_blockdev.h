/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The glue between the SD card driver and lwext4: an ext4_blockdev for the
 * whole card, and the partitions found in its MBR.
 */
#ifndef SD_BLOCKDEV_H_
#define SD_BLOCKDEV_H_

#include <ext4_blockdev.h>

/*
 * The whole card as an lwext4 block device, 512 byte sectors. Call after
 * sd_init(): the capacity comes from the card's CSD register.
 */
struct ext4_blockdev *sd_blockdev_card(void);

/*
 * Reads the MBR and returns partition n (0..3) as a block device, or NULL
 * if the slot is empty or not a Linux (0x83) partition.
 */
struct ext4_blockdev *sd_blockdev_partition(int n);

#endif /* SD_BLOCKDEV_H_ */
