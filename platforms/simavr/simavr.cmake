# SPDX-License-Identifier: BSD-3-Clause
# platforms/simavr: the ATmega1284 under simavr (README.md here). Defines
# platform_firmware(<target> <sources>...) and ${PLATFORM_RUNNER} <elf>, as
# every platforms/<name>/<name>.cmake does.

set(SIMAVR_DIR ${CMAKE_CURRENT_LIST_DIR})
set(PLATFORM_RUNNER simavr -m atmega1284 -f 16000000)

function(platform_firmware target)
    add_executable(${target} ${ARGN} ${SIMAVR_DIR}/platform.c)
    target_include_directories(${target} PRIVATE ${SIMAVR_DIR}/..)
    target_link_libraries(${target} lwext4)
    string(REGEX REPLACE "\\.elf$" "" map ${target})
    set_target_properties(${target} PROPERTIES LINK_FLAGS
        "-Wl,-Map=${map}.map")
endfunction()
