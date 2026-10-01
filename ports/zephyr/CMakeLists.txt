# SPDX-License-Identifier: BSD-3-Clause
#
# Zephyr module build of lwext4 (zephyr/module.yml points here). Builds the
# library sources of this repository into a Zephyr library when
# CONFIG_LWEXT4=y; the lwext4 CONFIG_* switches come from Kconfig through
# include/generated/ext4_config.h.

if(CONFIG_LWEXT4)
  set(lwext4_root ${ZEPHYR_CURRENT_MODULE_DIR})

  zephyr_library_named(lwext4)
  zephyr_library_sources(
    ${lwext4_root}/src/ext4.c
    ${lwext4_root}/src/ext4_balloc.c
    ${lwext4_root}/src/ext4_bcache.c
    ${lwext4_root}/src/ext4_bitmap.c
    ${lwext4_root}/src/ext4_block_group.c
    ${lwext4_root}/src/ext4_blockdev.c
    ${lwext4_root}/src/ext4_crc32.c
    ${lwext4_root}/src/ext4_debug.c
    ${lwext4_root}/src/ext4_dir.c
    ${lwext4_root}/src/ext4_dir_idx.c
    ${lwext4_root}/src/ext4_fs.c
    ${lwext4_root}/src/ext4_hash.c
    ${lwext4_root}/src/ext4_ialloc.c
    ${lwext4_root}/src/ext4_inode.c
    ${lwext4_root}/src/ext4_journal.c
    ${lwext4_root}/src/ext4_mbr.c
    ${lwext4_root}/src/ext4_mkfs.c
    ${lwext4_root}/src/ext4_super.c
    ${lwext4_root}/src/ext4_trans.c
    src/ext4_zephyr_lock.c
  )
  # These two files are GPLv2 (the rest of lwext4 is BSD-3-Clause). They
  # are only compiled when the feature is enabled.
  zephyr_library_sources_ifdef(CONFIG_LWEXT4_EXTENTS ${lwext4_root}/src/ext4_extent.c)
  zephyr_library_sources_ifdef(CONFIG_LWEXT4_XATTR ${lwext4_root}/src/ext4_xattr.c)
  # The block device on Zephyr's disk access API (SD cards, eMMC, NVMe,
  # RAM and flash disks, USB mass storage...)
  zephyr_library_sources_ifdef(CONFIG_LWEXT4_DISK_ACCESS src/ext4_zephyr_disk.c)

  # ext4.h and friends, ext4_zephyr.h, and generated/ext4_config.h (which
  # lwext4's include/ext4_config.h includes)
  zephyr_include_directories(${lwext4_root}/include include)
endif()
