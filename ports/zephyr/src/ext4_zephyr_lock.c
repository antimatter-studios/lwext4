/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Mount point locks for lwext4 on Zephyr. lwext4 calls lock()/unlock()
 * around every operation on a mount point; a k_mutex can be taken again by
 * the thread that holds it, which lwext4's nested calls need.
 */
#include <zephyr/kernel.h>

#include <ext4_zephyr.h>

static K_MUTEX_DEFINE(ext4_mp_mutex);

static void mp_lock(void)
{
	(void)k_mutex_lock(&ext4_mp_mutex, K_FOREVER);
}

static void mp_unlock(void)
{
	(void)k_mutex_unlock(&ext4_mp_mutex);
}

static const struct ext4_lock mp_locks = {
	.lock = mp_lock,
	.unlock = mp_unlock,
};

const struct ext4_lock *ext4_zephyr_mount_locks(void)
{
	return &mp_locks;
}
