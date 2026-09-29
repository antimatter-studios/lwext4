# SPDX-License-Identifier: BSD-3-Clause
# Helpers for the ci/jobs/package-*.sh release jobs (sourced after
# common.sh). Packages go to build-ci/dist/.

# Release version: the tag being built (v1.0.1-am.1 -> 1.0.1-am.1), or a
# describe string for untagged commits.
pkg_version()
{
	v=$(git describe --tags --always --dirty 2>/dev/null || echo unknown)
	echo "${v#v}"
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
