/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 build configuration for ESP-IDF.
 *
 * lwext4's include/ext4_config.h pulls in "generated/ext4_config.h"; for the
 * CMake build that file is written by the top level CMakeLists.txt. Here it
 * maps the Kconfig options of the lwext4 component (sdkconfig.h) onto the
 * lwext4 CONFIG_* switches instead.
 */
#ifndef LWEXT4_ESP_GENERATED_CONFIG_H_
#define LWEXT4_ESP_GENERATED_CONFIG_H_

#include "sdkconfig.h"

#if defined(CONFIG_LWEXT4_FEATURE_SET_EXT2)
#define CONFIG_EXT_FEATURE_SET_LVL 2
#elif defined(CONFIG_LWEXT4_FEATURE_SET_EXT3)
#define CONFIG_EXT_FEATURE_SET_LVL 3
#else
#define CONFIG_EXT_FEATURE_SET_LVL 4
#endif

#ifdef CONFIG_LWEXT4_JOURNALING
#define CONFIG_JOURNALING_ENABLE 1
#else
#define CONFIG_JOURNALING_ENABLE 0
#endif

#ifdef CONFIG_LWEXT4_XATTR
#define CONFIG_XATTR_ENABLE 1
#else
#define CONFIG_XATTR_ENABLE 0
#endif

#ifdef CONFIG_LWEXT4_EXTENTS
#define CONFIG_EXTENTS_ENABLE 1
#else
#define CONFIG_EXTENTS_ENABLE 0
#endif

#ifdef CONFIG_LWEXT4_DEBUG_PRINTF
#define CONFIG_DEBUG_PRINTF 1
#else
#define CONFIG_DEBUG_PRINTF 0
#endif

#ifdef CONFIG_LWEXT4_DEBUG_ASSERT
#define CONFIG_DEBUG_ASSERT 1
#else
#define CONFIG_DEBUG_ASSERT 0
#endif

#ifdef CONFIG_LWEXT4_BLOCKDEV_STATS
#define CONFIG_BLOCK_DEV_ENABLE_STATS 1
#else
#define CONFIG_BLOCK_DEV_ENABLE_STATS 0
#endif

#define CONFIG_BLOCK_DEV_CACHE_SIZE CONFIG_LWEXT4_CACHE_SIZE
#define CONFIG_EXT4_BLOCKDEVS_COUNT CONFIG_LWEXT4_BLOCKDEVS_COUNT
#define CONFIG_EXT4_MOUNTPOINTS_COUNT CONFIG_LWEXT4_MOUNTPOINTS_COUNT
#define CONFIG_EXT4_MAX_BLOCKDEV_NAME CONFIG_LWEXT4_MAX_NAME
#define CONFIG_EXT4_MAX_MP_NAME CONFIG_LWEXT4_MAX_NAME
#define CONFIG_MAX_TRUNCATE_SIZE ((unsigned long)CONFIG_LWEXT4_MAX_TRUNCATE_SIZE)

/* newlib provides errno values, open flags and assert(). */
#define CONFIG_HAVE_OWN_ERRNO 0
#define CONFIG_HAVE_OWN_OFLAGS 0
#define CONFIG_HAVE_OWN_ASSERT 1

/*
 * Xtensa LX6/LX7 and the RISC-V cores of the ESP32 family fault (or trap to
 * a slow emulation handler) on unaligned 32-bit accesses to some memories;
 * keep lwext4 on its byte-wise accessors.
 */
#define CONFIG_UNALIGNED_ACCESS 0
#define CONFIG_USE_USER_MALLOC 0

#endif /* LWEXT4_ESP_GENERATED_CONFIG_H_ */
