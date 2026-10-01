# SPDX-License-Identifier: BSD-3-Clause
# lwext4 example and on-target test for ESP-IDF (esp32, esp32c3, esp32s3).
#
# Built with idf.py like any other ESP-IDF application and run in Espressif's
# QEMU (idf.py qemu) or on a real board. See README.md.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../ports/esp-idf")
# Keep the build lean: only what the test needs (plus its dependencies).
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lwext4_example)

# The "ext4host" data partition is pre-populated with a filesystem made by
# mke2fs on the build host and flashed alongside the application, the same
# way a user ships a prebuilt data image (idf.py flash / idf.py qemu).
partition_table_get_partition_info(ext4host_size "--partition-name ext4host" "size")
set(ext4host_img "${CMAKE_BINARY_DIR}/ext4host.img")
add_custom_command(
    OUTPUT "${ext4host_img}"
    COMMAND "${CMAKE_CURRENT_LIST_DIR}/host/mkimage.sh" flash
            "${ext4host_img}" "${ext4host_size}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/host/mkimage.sh"
            "${CMAKE_CURRENT_LIST_DIR}/host/pattern.py"
    COMMENT "Building host-made ext4 image for the ext4host partition"
    VERBATIM)
add_custom_target(ext4host_image ALL DEPENDS "${ext4host_img}")
esptool_py_flash_to_partition(flash "ext4host" "${ext4host_img}")
