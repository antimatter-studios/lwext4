# SPDX-License-Identifier: BSD-3-Clause
#
# Example firmware (README.md here) on the MPS2 boards (platforms/mps2):
# each application is built for the toolchain's CPU, and CTest runs it in
# QEMU on a disk image and checks the image with e2fsprogs (check.sh).
include(${PROJECT_SOURCE_DIR}/platforms/platform.cmake)

foreach(app hello datalogger reader)
    platform_firmware(${app}.elf ${app}.c)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${app})
    add_test(NAME firmware-${app}
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check.sh ${app}
                     $<TARGET_FILE:${app}.elf>
                     ${LWEXT4_QEMU_SYSTEM_ARM} ${LWEXT4_QEMU_MACHINE}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${app})
    set_tests_properties(firmware-${app} PROPERTIES TIMEOUT 1800)
endforeach()
