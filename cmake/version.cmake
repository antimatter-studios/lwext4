# SPDX-License-Identifier: BSD-3-Clause
#
# The version of lwext4 is its release tag, vMAJOR.MINOR.PATCH on main
# (CONTRIBUTING.md); nothing in the tree states it. A build takes it, when
# it is configured, from the first of
#
#   -DLWEXT4_VERSION=<v>  a build that has neither of the last two
#   $LWEXT4_VERSION       (ci/run.sh passes its containers the host's: a
#                         worktree's git directory is not in the mount)
#   git describe          a clone of the repository: the tag, or
#                         <tag>-<commits since>-g<hash>, with -dirty for
#                         changes to tracked files
#   cmake/version.txt     a source archive: git archive (GitHub's "Source
#                         code" downloads too) writes git describe into it
#                         (export-subst in .gitattributes)
#
# and is 0.0.0-unknown without any of them: a warning, but an error on
# GitHub Actions, whose builds must report the version of their tag.
#
# lwext4_project_version() sets LWEXT4_VERSION_STRING (1.1.0, or
# 1.1.0-3-gabc1234 between releases) and VERSION_MAJOR, VERSION_MINOR and
# VERSION_PATCH, its first three numbers (the version of the CMake package
# and of the pkg-config file).

find_package(Git QUIET)

# lwext4_version(<var> <source dir> <override>): the version of the tree in
# <source dir>, without the v of the tag; "" if it has none.
function(lwext4_version var dir override)
    set(v "${override}")
    if(v STREQUAL "")
        set(v "$ENV{LWEXT4_VERSION}")
    endif()
    # Only if <dir> is the top of its repository: a copy of lwext4 inside
    # another project's repository is not described by that project's tags.
    if(v STREQUAL "" AND GIT_EXECUTABLE)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --show-toplevel
            WORKING_DIRECTORY "${dir}" RESULT_VARIABLE r
            OUTPUT_VARIABLE top OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(r EQUAL 0)
            get_filename_component(top "${top}" REALPATH)
            get_filename_component(real "${dir}" REALPATH)
            if(top STREQUAL real)
                execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags
                        --match "v[0-9]*" --dirty
                    WORKING_DIRECTORY "${dir}" RESULT_VARIABLE r
                    OUTPUT_VARIABLE v OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
                if(NOT r EQUAL 0)
                    set(v "")
                endif()
            endif()
        endif()
    endif()
    if(v STREQUAL "" AND EXISTS "${dir}/cmake/version.txt")
        file(STRINGS "${dir}/cmake/version.txt" v LIMIT_COUNT 1)
        # Not substituted: a checkout, not an archive
        if(v MATCHES "^\\$Format")
            set(v "")
        endif()
    endif()
    string(REGEX REPLACE "^v" "" v "${v}")
    set(${var} "${v}" PARENT_SCOPE)
endfunction()

# lwext4_version_parts(<version> <prefix>): <prefix>_MAJOR, _MINOR and
# _PATCH, the first three numbers of <version>; "" if it does not start
# with them.
function(lwext4_version_parts version prefix)
    set(major "")
    set(minor "")
    set(patch "")
    if(version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(major ${CMAKE_MATCH_1})
        set(minor ${CMAKE_MATCH_2})
        set(patch ${CMAKE_MATCH_3})
    endif()
    set(${prefix}_MAJOR "${major}" PARENT_SCOPE)
    set(${prefix}_MINOR "${minor}" PARENT_SCOPE)
    set(${prefix}_PATCH "${patch}" PARENT_SCOPE)
endfunction()

macro(lwext4_project_version)
    set(LWEXT4_VERSION "" CACHE STRING
        "Version of lwext4; empty: from its release tag (cmake/version.cmake)")
    lwext4_version(LWEXT4_VERSION_STRING "${PROJECT_SOURCE_DIR}"
                   "${LWEXT4_VERSION}")
    if(LWEXT4_VERSION_STRING STREQUAL "")
        if(NOT "$ENV{GITHUB_ACTIONS}" STREQUAL "")
            message(FATAL_ERROR "lwext4: no version: the clone has no v* "
                "tag (cmake/version.cmake)")
        endif()
        message(WARNING "lwext4: no version: neither a clone with its tags "
            "nor a source archive; pass -DLWEXT4_VERSION=<version>")
        set(LWEXT4_VERSION_STRING 0.0.0-unknown)
    endif()
    lwext4_version_parts("${LWEXT4_VERSION_STRING}" VERSION)
    if(VERSION_MAJOR STREQUAL "")
        message(FATAL_ERROR "lwext4: version '${LWEXT4_VERSION_STRING}' "
            "does not start with MAJOR.MINOR.PATCH")
    endif()
    message(STATUS "lwext4 ${LWEXT4_VERSION_STRING}")
endmacro()
