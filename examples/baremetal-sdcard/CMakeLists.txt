# SPDX-License-Identifier: BSD-3-Clause
#
# lwext4 on hobby microcontroller boards with an SD card on SPI.
#
#   cmake -S examples/baremetal-sdcard -B build/nucleo_f401re -DBOARD=nucleo_f401re
#   cmake --build build/nucleo_f401re
#   tests/renode/run.sh build/nucleo_f401re     # emulated test, needs Renode
#
# or, with nothing but docker installed (see README.md):
#
#   ci/run.sh baremetal-sdcard-build nucleo_f401re
#   ci/run.sh renode-baremetal-sdcard nucleo_f401re
#
# The firmware is built the way a product would be: -Os, newlib-nano,
# unused sections garbage collected, lwext4 without debug output, the real
# chip's flash/RAM sizes in the linker script.
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/arm-none-eabi.cmake)
endif()

project(lwext4_baremetal_sdcard C)

set(BOARD nucleo_f401re CACHE STRING "Board, a directory of platforms/ with a board.cmake")
option(WRITE_BACK_CACHE "Enable lwext4's write back block cache (faster, more RAM)" OFF)
set(BIG_FILE_KIB 256 CACHE STRING "Size of the large file the test writes")

get_filename_component(LWEXT4_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
# The board code: platforms/<board> (clocks, pins, UART, SPI) on
# platforms/sdcard (start-up, SD card over SPI, block device)
set(BOARD_DIR ${LWEXT4_DIR}/platforms/${BOARD})
set(SDCARD_DIR ${LWEXT4_DIR}/platforms/sdcard)
if(NOT EXISTS ${BOARD_DIR}/board.cmake)
    message(FATAL_ERROR "Unknown board '${BOARD}'")
endif()
# BOARD_CPU_FLAGS, BOARD_DEFS, BOARD_RENODE_*
include(${BOARD_DIR}/board.cmake)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()
# -fmacro-prefix-map: assert messages carry "src/ext4_fs.c", not the full
# build path, in flash
set(COMMON_FLAGS ${BOARD_CPU_FLAGS} -ffunction-sections -fdata-sections -g
    -fmacro-prefix-map=${LWEXT4_DIR}/=)

# ---------------------------------------------------------------------------
# lwext4, configured through CONFIG_* defines (include/ext4_config.h lists
# them all with their defaults).
# ---------------------------------------------------------------------------
set(LWEXT4_DEFS
    # take the defaults from ext4_config.h, no generated config header
    CONFIG_USE_DEFAULT_CFG=1
    # no debug printf: saves ~20 KiB of flash
    CONFIG_DEBUG_PRINTF=0
    # keep the internal consistency checks, reported through newlib assert()
    CONFIG_DEBUG_ASSERT=1
    CONFIG_HAVE_OWN_ASSERT=0
    # newlib provides errno values and open() flags
    CONFIG_HAVE_OWN_ERRNO=0
    CONFIG_HAVE_OWN_OFLAGS=1
    # blocks kept in the block cache (default 8): RAM = n * block size
    CONFIG_BLOCK_DEV_CACHE_SIZE=${BOARD_CACHE_SIZE}
    ${BOARD_DEFS})
if(NOT BOARD_CACHE_SIZE)
    list(FILTER LWEXT4_DEFS EXCLUDE REGEX "^CONFIG_BLOCK_DEV_CACHE_SIZE=$")
endif()

file(GLOB LWEXT4_SRC ${LWEXT4_DIR}/src/*.c)
add_library(lwext4 STATIC ${LWEXT4_SRC})
target_include_directories(lwext4 PUBLIC ${LWEXT4_DIR}/include)
target_compile_definitions(lwext4 PUBLIC ${LWEXT4_DEFS})
# -fstack-usage: per function stack frames in *.su next to the objects
target_compile_options(lwext4 PRIVATE ${COMMON_FLAGS} -Wall -fstack-usage)

# ---------------------------------------------------------------------------
# The firmware
# ---------------------------------------------------------------------------
set(FW firmware.elf)
add_executable(${FW}
    src/main.c
    src/workload.c
    ${SDCARD_DIR}/sd_blockdev.c
    ${SDCARD_DIR}/sd_spi.c
    ${SDCARD_DIR}/startup.c
    ${BOARD_DIR}/board.c)
target_include_directories(${FW} PRIVATE src ${SDCARD_DIR})
target_compile_options(${FW} PRIVATE ${COMMON_FLAGS} -Wall -Wextra)
target_compile_definitions(${FW} PRIVATE
    BIG_FILE_KIB=${BIG_FILE_KIB}
    WRITE_BACK_CACHE=$<BOOL:${WRITE_BACK_CACHE}>)
target_link_libraries(${FW} lwext4)
target_link_options(${FW} PRIVATE
    ${BOARD_CPU_FLAGS}
    --specs=nano.specs
    -nostartfiles
    -Wl,--gc-sections
    -Wl,-L${BOARD_DIR}
    -T${SDCARD_DIR}/cortex-m.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
    -Wl,--print-memory-usage)
set_target_properties(${FW} PROPERTIES LINK_DEPENDS
    "${SDCARD_DIR}/cortex-m.ld;${BOARD_DIR}/memory.ld")

# Flashable images (firmware.hex, firmware.bin) and a flash/RAM breakdown
# per component (footprint.txt).
find_program(OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_package(Python3 COMPONENTS Interpreter)
add_custom_command(TARGET ${FW} POST_BUILD
    COMMAND ${OBJCOPY} -O ihex ${FW} firmware.hex
    COMMAND ${OBJCOPY} -O binary ${FW} firmware.bin
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/footprint.py
            firmware.map > footprint.txt
    BYPRODUCTS firmware.hex firmware.bin footprint.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Board description for the Renode test harness (tests/renode/run.sh).
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/board.env
    "BOARD=${BOARD}\n"
    "BOARD_REPL=${BOARD_DIR}/board.repl\n"
    "BOARD_UART=${BOARD_RENODE_UART}\n"
    "BOARD_SPI=${BOARD_RENODE_SPI}\n"
    "BIG_FILE_KIB=${BIG_FILE_KIB}\n"
    "BOARD_TEST_EXCLUDE=\"${BOARD_TEST_EXCLUDE}\"\n"
    "FIRMWARE=${CMAKE_CURRENT_BINARY_DIR}/firmware.hex\n"
    "ELF=${CMAKE_CURRENT_BINARY_DIR}/${FW}\n")
