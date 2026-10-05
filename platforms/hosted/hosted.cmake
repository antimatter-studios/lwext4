# SPDX-License-Identifier: BSD-3-Clause
# platforms/hosted: simulators whose C library has a console and exit()
# (README.md here). Defines platform_firmware(<target> <sources>...) and
# ${PLATFORM_RUNNER} <elf>, as every platforms/<name>/<name>.cmake does.
#
#   arm-sim     ARM7TDMI with newlib's rdimon semihosting, run by qemu-arm
#   msp430-sim  MSP430X large model with libgloss, run by msp430-elf-run

set(HOSTED_DIR ${CMAKE_CURRENT_LIST_DIR})

if(CMAKE_SYSTEM_PROCESSOR STREQUAL arm-sim)
    set(LWEXT4_QEMU_ARM qemu-arm CACHE STRING
        "qemu-arm (user mode) binary used to run the arm-sim firmware")
    # The firmware is linked at 0x8000, below the usual vm.mmap_min_addr of
    # 64 KiB: give qemu a guest base so the host mapping lands elsewhere.
    set(PLATFORM_RUNNER ${LWEXT4_QEMU_ARM} -B 0x40000000)
    set(hosted_link_flags "")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL msp430-sim)
    set(PLATFORM_RUNNER ${MSP430_PREFIX}-run)
    set(hosted_link_flags "-msim")
else()
    message(FATAL_ERROR "platforms/hosted: not for ${CMAKE_SYSTEM_PROCESSOR}")
endif()

function(platform_firmware target)
    add_executable(${target} ${ARGN} ${HOSTED_DIR}/platform.c)
    target_include_directories(${target} PRIVATE ${HOSTED_DIR}/..)
    target_link_libraries(${target} lwext4)
    string(REGEX REPLACE "\\.elf$" "" map ${target})
    set_target_properties(${target} PROPERTIES LINK_FLAGS
        "${hosted_link_flags} -Wl,-Map=${map}.map")
endfunction()
