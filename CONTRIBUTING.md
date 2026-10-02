Contributing to antimatter-studios/lwext4
=========================================

This fork is where lwext4 is developed: issues are tracked here, fixes are
pull requests into `main`, and releases are tagged on `main`.

Issues
------

- Every bug or piece of work gets an issue in this repository first,
  including findings of the fuzzer, the fault injection sweep and the
  stress tests.
- Issues copied from [gkostka/lwext4](https://github.com/gkostka/lwext4)
  carry the `upstream-copy` label and name the upstream issue in their
  first line.
- Write upstream issue and pull request numbers as "upstream #73". A bare
  `#73` in an issue, pull request or commit message means issue 73 of
  this repository, and "Fixes #73" in a pull request closes it.

Pull requests
-------------

- Branch from `main`, one fix or feature per branch, named by topic
  (`fix/...`, `feature/...`, `tests/...`, `ci/...`, `docs/...`).
- Open the pull request against `main`, ready for review, and name the
  issue it closes ("Fixes #N").
- A fix comes with a regression test (`tests/test_*.c`, registered in
  [tests/CMakeLists.txt](tests/CMakeLists.txt)). The CI job `red-green`
  builds the new tests against the base of the branch and fails if one of
  them passes there, i.e. if it does not test the fix. A test that must pass
  on the base as well (a guard) says so in a `red-green: guard` comment.
- Every CI job must pass; there are no jobs that are allowed to fail.
  Every job runs in a container, so `ci/run.sh <job>` reproduces it
  locally (`ci/run.sh --list`).
- Coverage can only go up: if a change raises it, raise
  [ci/coverage-floor](ci/coverage-floor) to the new measured values,
  rounded down to a whole percent.
- Licences do not change. The library is BSD-3-Clause except
  `src/ext4_extent.c` and `src/ext4_xattr.c` (GPL-2.0). New files take the
  licence of the code they build on; tests, CI scripts and glue are
  BSD-3-Clause (`SPDX-License-Identifier: BSD-3-Clause`).

Releases
--------

Tag a commit of `main` as `v<next upstream patch>-am.<n>` (see the
README) and push the tag. The release workflow runs the whole CI matrix
on it, builds the packages and publishes the release, with a manifest of
the pull requests merged since the previous release, only if every job
passed.
