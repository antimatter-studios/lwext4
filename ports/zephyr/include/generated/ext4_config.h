/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 build configuration for Zephyr.
 *
 * lwext4's include/ext4_config.h includes "generated/ext4_config.h"; for
 * lwext4's own CMake build that file is written by the top level
 * CMakeLists.txt. In a Zephyr build this one is found instead: it maps the
 * Kconfig options of the lwext4 module (ports/zephyr/Kconfig, available in
 * every file through Zephyr's autoconf.h) onto lwext4's CONFIG_* switches.
 * None of lwext4's switch names is a Zephyr Kconfig symbol, except
 * CONFIG_BIG_ENDIAN, which means the same in both.
 */
#ifndef LWEXT4_ZEPHYR_GENERATED_CONFIG_H_
#define LWEXT4_ZEPHYR_GENERATED_CONFIG_H_

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

/* Zephyr's C libraries (picolibc, newlib) provide the errno values and
 * assert(); lwext4's own open() flags are used, because the minimal libc
 * has no <fcntl.h>. */
#define CONFIG_HAVE_OWN_ERRNO 0
#define CONFIG_HAVE_OWN_OFLAGS 1
#define CONFIG_HAVE_OWN_ASSERT 0

/* Byte-wise accessors for on-disk fields: safe on every core (Cortex-M0
 * and many RISC-V cores fault on unaligned 32 bit loads). */
#define CONFIG_UNALIGNED_ACCESS 0
#define CONFIG_USE_USER_MALLOC 0

#endif /* LWEXT4_ZEPHYR_GENERATED_CONFIG_H_ */
