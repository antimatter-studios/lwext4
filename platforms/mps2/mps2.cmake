# SPDX-License-Identifier: BSD-3-Clause
# platforms/mps2: firmware for the Arm MPS2 boards that QEMU emulates
# (README.md here), for the cortex-m* toolchains. Defines, as every
# platforms/<name>/<name>.cmake does:
#
#   platform_firmware(<target> <sources>...)  an executable linked with
#                                             lwext4 and the board code
#   ${PLATFORM_RUNNER} <elf>                  runs one in QEMU; the exit
#                                             status is the firmware's
#
# The board: -DLWEXT4_QEMU_MACHINE, by default the one of the toolchain's
# CPU. ARMv6-M (Cortex-M0/M0+) code also runs on the Cortex-M3 of the AN385.

set(MPS2_DIR ${CMAKE_CURRENT_LIST_DIR})

set(LWEXT4_QEMU_SYSTEM_ARM qemu-system-arm CACHE STRING
    "qemu-system-arm binary used to run the MPS2 firmware")
if(CMAKE_C_FLAGS MATCHES "-mcpu=cortex-m7")
    set(mps2_default_machine mps2-an500)
elseif(CMAKE_C_FLAGS MATCHES "-mcpu=cortex-m4")
    set(mps2_default_machine mps2-an386)
else()
    set(mps2_default_machine mps2-an385)
endif()
set(LWEXT4_QEMU_MACHINE ${mps2_default_machine} CACHE STRING
    "QEMU machine (-M) for the MPS2 firmware")

set(PLATFORM_RUNNER ${LWEXT4_QEMU_SYSTEM_ARM} -M ${LWEXT4_QEMU_MACHINE}
    -nographic -monitor none -serial none
    -semihosting-config enable=on,target=native -kernel)

function(platform_firmware target)
    add_executable(${target} ${ARGN} ${MPS2_DIR}/startup.c ${MPS2_DIR}/counter.c
                   ${MPS2_DIR}/disk.c)
    target_include_directories(${target} PRIVATE ${MPS2_DIR}/..)
    target_link_libraries(${target} lwext4)
    string(REGEX REPLACE "\\.elf$" "" map ${target})
    set_target_properties(${target} PROPERTIES LINK_FLAGS
        "--specs=rdimon.specs -T${MPS2_DIR}/mps2.ld -Wl,-Map=${map}.map")
endfunction()
