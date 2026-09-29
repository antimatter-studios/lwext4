# SPDX-License-Identifier: BSD-3-Clause
# ESP-IDF component for lwext4.
#
# Add the parent directory to your project's EXTRA_COMPONENT_DIRS:
#
#   set(EXTRA_COMPONENT_DIRS "<path to lwext4>/ports/esp-idf")
#
# and list `lwext4` in the REQUIRES/PRIV_REQUIRES of the component that uses
# it. The library sources are taken straight from the lwext4 tree; the
# lwext4 CONFIG_* options are derived from Kconfig (idf.py menuconfig ->
# Component config -> lwext4) by include/generated/ext4_config.h.

set(lwext4_root "${COMPONENT_DIR}/../../..")

set(srcs
    "${lwext4_root}/src/ext4.c"
    "${lwext4_root}/src/ext4_balloc.c"
    "${lwext4_root}/src/ext4_bcache.c"
    "${lwext4_root}/src/ext4_bitmap.c"
    "${lwext4_root}/src/ext4_block_group.c"
    "${lwext4_root}/src/ext4_blockdev.c"
    "${lwext4_root}/src/ext4_crc32.c"
    "${lwext4_root}/src/ext4_debug.c"
    "${lwext4_root}/src/ext4_dir.c"
    "${lwext4_root}/src/ext4_dir_idx.c"
    "${lwext4_root}/src/ext4_fs.c"
    "${lwext4_root}/src/ext4_hash.c"
    "${lwext4_root}/src/ext4_ialloc.c"
    "${lwext4_root}/src/ext4_inode.c"
    "${lwext4_root}/src/ext4_journal.c"
    "${lwext4_root}/src/ext4_mbr.c"
    "${lwext4_root}/src/ext4_mkfs.c"
    "${lwext4_root}/src/ext4_super.c"
    "${lwext4_root}/src/ext4_trans.c"
    "port/ext4_esp_blockdev.c"
    "port/ext4_esp_lock.c")

# These two files are GPLv2 (the rest of lwext4 is BSD-3-Clause). They are
# only compiled when the feature is enabled in menuconfig.
if(CONFIG_LWEXT4_EXTENTS)
    list(APPEND srcs "${lwext4_root}/src/ext4_extent.c")
endif()
if(CONFIG_LWEXT4_XATTR)
    list(APPEND srcs "${lwext4_root}/src/ext4_xattr.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include" "${lwext4_root}/include"
    REQUIRES esp_partition sdmmc
    PRIV_REQUIRES freertos)

# lwext4 is plain C99 with a few benign warnings under -Wall -Wextra that
# ESP-IDF turns into errors by default; keep them visible but non-fatal.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=unused-parameter
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-function
    -Wno-error=sign-compare
    -Wno-error=format
    -Wno-error=implicit-fallthrough
    -Wno-error=maybe-uninitialized)
