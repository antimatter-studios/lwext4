# SPDX-License-Identifier: BSD-3-Clause
# test_memory records every allocation of the library through
# CONFIG_USE_USER_MALLOC=1: build the library with the hook and link the
# test against it instead of the default library (as test_user_malloc).
if(NOT TARGET lwext4_user_malloc)
    file(GLOB lwext4_user_malloc_src ${PROJECT_SOURCE_DIR}/src/*.c)
    add_library(lwext4_user_malloc STATIC ${lwext4_user_malloc_src})
    target_include_directories(lwext4_user_malloc PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(lwext4_user_malloc PUBLIC CONFIG_USE_USER_MALLOC=1)
endif()

target_sources(${test_name} PRIVATE common/test_util.c common/mem_sim.c)
target_include_directories(${test_name} PRIVATE common)
set_property(TARGET ${test_name} PROPERTY LINK_LIBRARIES lwext4_user_malloc blockdev)
