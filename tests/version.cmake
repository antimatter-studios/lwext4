# SPDX-License-Identifier: BSD-3-Clause
#
# cmake/version.cmake (fork issue #223): a build takes its version from,
# in order, -DLWEXT4_VERSION, $LWEXT4_VERSION, git describe of the tree's
# own repository (v* tags only), and the version that git archive writes
# into cmake/version.txt; its first three numbers are the package version.
#
#   cmake -DSOURCE_DIR=<lwext4> -DWORK=<scratch dir> -DCASES=plain|git \
#         -P version.cmake
#
# plain (CTest test version): what needs no git, as in a source archive.
# git (version_git): on a scratch repository with tags, and on archives of
# it made by git archive; skipped where git is not installed (the README
# does not ask for it).

cmake_minimum_required(VERSION 3.4) # the policies of the project
include(${SOURCE_DIR}/cmake/version.cmake)
# ci/run.sh sets it for the build; here it would hide everything else.
unset(ENV{LWEXT4_VERSION})

# What cmake/version.txt holds in a checkout
set(placeholder "$Format:%(describe:tags=true,match=v[0-9]*)$")

# A failed check is reported and the others still run; cmake then exits
# with an error.
function(expect what dir override want)
    lwext4_version(got "${dir}" "${override}")
    if(got STREQUAL want)
        message(STATUS "ok: ${what}: '${got}'")
    else()
        message(SEND_ERROR "${what}: '${got}', expected '${want}'")
    endif()
endfunction()

function(expect_match what dir regex)
    lwext4_version(got "${dir}" "")
    if(got MATCHES "${regex}")
        message(STATUS "ok: ${what}: '${got}'")
    else()
        message(SEND_ERROR "${what}: '${got}', expected ${regex}")
    endif()
endfunction()

function(expect_parts version want)
    lwext4_version_parts("${version}" p)
    set(got "${p_MAJOR} ${p_MINOR} ${p_PATCH}")
    if(got STREQUAL want)
        message(STATUS "ok: parts of '${version}': ${got}")
    else()
        message(SEND_ERROR "parts of '${version}': '${got}', expected '${want}'")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK}")

if(CASES STREQUAL plain)
    expect_parts(1.1.0 "1 1 0")
    expect_parts(1.0.1-am.5-21-g34503da "1 0 1")
    expect_parts(10.20.30-4-gabc1234-dirty "10 20 30")
    expect_parts(0.0.0-unknown "0 0 0")
    expect_parts(1.1 "  ")

    # The tree's own file is the placeholder, or a version if this is a
    # source archive.
    file(STRINGS ${SOURCE_DIR}/cmake/version.txt own LIMIT_COUNT 1)
    if(own STREQUAL placeholder OR own MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+")
        message(STATUS "ok: cmake/version.txt: '${own}'")
    else()
        message(SEND_ERROR "cmake/version.txt: '${own}'")
    endif()

    file(WRITE ${WORK}/archive/cmake/version.txt "v1.1.0-1-gabc1234\n")
    file(WRITE ${WORK}/checkout/cmake/version.txt "${placeholder}\n")
    file(MAKE_DIRECTORY ${WORK}/none)
    expect_match("a source archive" ${WORK}/archive "^1\\.1\\.0-1-gabc1234$")
    expect("a checkout without git: not substituted" ${WORK}/checkout "" "")
    expect("nothing to go by" ${WORK}/none "" "")
    expect("-DLWEXT4_VERSION first" ${WORK}/archive v2.0.0 2.0.0)
    set(ENV{LWEXT4_VERSION} 3.0.0)
    expect("then \$LWEXT4_VERSION" ${WORK}/archive "" 3.0.0)
    unset(ENV{LWEXT4_VERSION})

elseif(CASES STREQUAL git)
    if(NOT GIT_EXECUTABLE)
        # SKIP_REGULAR_EXPRESSION of the test (tests/CMakeLists.txt)
        message(STATUS "git not found: skipped")
        return()
    endif()

    function(run_git dir)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -c user.name=lwext4
                -c user.email=lwext4@localhost -c commit.gpgsign=false
                -c tag.gpgsign=false ${ARGN}
            WORKING_DIRECTORY "${dir}" RESULT_VARIABLE r OUTPUT_QUIET
            ERROR_VARIABLE err)
        if(NOT r EQUAL 0)
            message(FATAL_ERROR "git ${ARGN}: ${err}")
        endif()
    endfunction()

    # A repository with the files of the tree that make the archive
    # version (the placeholder itself if the tree is an archive).
    set(repo "${WORK}/repo")
    file(STRINGS ${SOURCE_DIR}/cmake/version.txt own LIMIT_COUNT 1)
    if(NOT own MATCHES "^\\$Format")
        set(own "${placeholder}")
    endif()
    file(WRITE ${repo}/cmake/version.txt "${own}\n")
    configure_file(${SOURCE_DIR}/.gitattributes ${repo}/.gitattributes
                   COPYONLY)
    file(WRITE ${repo}/file "one\n")
    run_git(${repo} init -q)
    run_git(${repo} add .)
    run_git(${repo} commit -q -m one)

    expect("no tag, a checkout: not substituted" ${repo} "" "")
    run_git(${repo} tag -a -m am.5 v1.0.1-am.5)
    expect("the old tags" ${repo} "" 1.0.1-am.5)
    run_git(${repo} commit -q --allow-empty -m two)
    run_git(${repo} tag not-a-version)
    expect_match("past a tag, other tags ignored" ${repo}
                 "^1\\.0\\.1-am\\.5-1-g[0-9a-f]+$")
    run_git(${repo} tag -a -m 1.1.0 v1.1.0)
    expect("a release tag" ${repo} "" 1.1.0)
    run_git(${repo} commit -q --allow-empty -m three)
    expect_match("past a release" ${repo} "^1\\.1\\.0-1-g[0-9a-f]+$")
    file(WRITE ${repo}/file "changed\n")
    expect_match("changed files" ${repo} "^1\\.1\\.0-1-g[0-9a-f]+-dirty$")
    run_git(${repo} checkout -q -- file)
    expect("-DLWEXT4_VERSION before git" ${repo} v2.0.0 2.0.0)
    set(ENV{LWEXT4_VERSION} 3.0.0)
    expect("\$LWEXT4_VERSION before git" ${repo} "" 3.0.0)
    unset(ENV{LWEXT4_VERSION})

    # A copy of lwext4 inside another project's repository
    file(WRITE ${repo}/sub/cmake/version.txt "${own}\n")
    expect("not the top of the repository" ${repo}/sub "" "")

    # Source archives, of a commit and of the release tag
    file(MAKE_DIRECTORY ${WORK}/archive ${WORK}/archive-tag)
    run_git(${repo} archive --format=tar -o ${WORK}/head.tar HEAD)
    run_git(${repo} archive --format=tar -o ${WORK}/tag.tar v1.1.0)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${WORK}/head.tar
                    WORKING_DIRECTORY ${WORK}/archive)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${WORK}/tag.tar
                    WORKING_DIRECTORY ${WORK}/archive-tag)
    expect_match("archive of a commit" ${WORK}/archive
                 "^1\\.1\\.0-1-g[0-9a-f]+$")
    expect("archive of the tag" ${WORK}/archive-tag "" 1.1.0)

else()
    message(FATAL_ERROR "CASES must be plain or git")
endif()

file(REMOVE_RECURSE "${WORK}")
