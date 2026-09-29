/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Mount point locks for lwext4 on FreeRTOS.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ext4_esp.h"

static SemaphoreHandle_t mp_mutex;
static StaticSemaphore_t mp_mutex_buf;

static void mp_lock(void)
{
	xSemaphoreTakeRecursive(mp_mutex, portMAX_DELAY);
}

static void mp_unlock(void)
{
	xSemaphoreGiveRecursive(mp_mutex);
}

static const struct ext4_lock mp_locks = {
	.lock = mp_lock,
	.unlock = mp_unlock,
};

const struct ext4_lock *ext4_esp_mount_locks(void)
{
	static portMUX_TYPE init_mux = portMUX_INITIALIZER_UNLOCKED;

	taskENTER_CRITICAL(&init_mux);
	if (!mp_mutex)
		mp_mutex = xSemaphoreCreateRecursiveMutexStatic(&mp_mutex_buf);
	taskEXIT_CRITICAL(&init_mux);
	return &mp_locks;
}
