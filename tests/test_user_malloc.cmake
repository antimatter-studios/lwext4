# SPDX-License-Identifier: BSD-3-Clause
# CONFIG_USE_USER_MALLOC=1: build the library with the application's
# allocator (ext4_user_malloc and friends, defined by the test) and link the
# test against it instead of the default library.
file(GLOB lwext4_user_malloc_src ${PROJECT_SOURCE_DIR}/src/*.c)
add_library(lwext4_user_malloc STATIC ${lwext4_user_malloc_src})
target_include_directories(lwext4_user_malloc PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(lwext4_user_malloc PUBLIC CONFIG_USE_USER_MALLOC=1)

target_sources(${test_name} PRIVATE common/test_util.c)
target_include_directories(${test_name} PRIVATE common)
set_property(TARGET ${test_name} PROPERTY LINK_LIBRARIES lwext4_user_malloc blockdev)
