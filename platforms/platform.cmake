# SPDX-License-Identifier: BSD-3-Clause
# The platform of the toolchain (README.md here): include() it to get
#
#   platform_firmware(<target> <sources>...)  an executable linked with
#                                             lwext4 and the board code
#   ${PLATFORM_RUNNER} <elf>                  runs it; the exit status (or,
#                                             on simavr, the output) is the
#                                             firmware's
#   ${PLATFORM_NAME}                          mps2, hosted or simavr

# Start-up files are needed, whatever the library-only toolchain says.
string(REPLACE "-nostartfiles" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^cortex-m")
    set(PLATFORM_NAME mps2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm-sim|msp430-sim)$")
    set(PLATFORM_NAME hosted)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL atmega1284)
    set(PLATFORM_NAME simavr)
else()
    message(FATAL_ERROR "platforms: none for ${CMAKE_SYSTEM_PROCESSOR}")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/${PLATFORM_NAME}/${PLATFORM_NAME}.cmake)
