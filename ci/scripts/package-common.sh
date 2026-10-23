# SPDX-License-Identifier: BSD-3-Clause
# Helpers for the ci/jobs/package-*.sh release jobs (sourced after
# common.sh). Packages go to build-ci/dist/.

# Release version: LWEXT4_VERSION, which ci/run.sh takes from git describe
# on the host (ci/scripts/version.sh): the tag being built (v1.1.0 ->
# 1.1.0), or <tag>-<n>-g<hash> for untagged commits. The build takes the
# same (cmake/version.cmake).
pkg_version()
{
	echo "${LWEXT4_VERSION:-unknown}"
}

pkg_dist()
{
	mkdir -p build-ci/dist
	echo "build-ci/dist"
}

# pkg_tar <staging dir> <package name>: build-ci/dist/<name>.tar.gz whose
# top level directory is <name>.
pkg_tar()
{
	dist=$(pkg_dist)
	rm -rf "build-ci/pkg/$2"
	mkdir -p build-ci/pkg
	cp -a "$1" "build-ci/pkg/$2"
	cp LICENSE "build-ci/pkg/$2/"
	tar -C build-ci/pkg -czf "$dist/$2.tar.gz" "$2"
	echo "packaged $dist/$2.tar.gz"
}
