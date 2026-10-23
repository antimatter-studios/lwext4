# SPDX-License-Identifier: BSD-3-Clause
# ci_version <repository root>: the version of the tree, as
# cmake/version.cmake takes it from git (1.1.0, or 1.1.0-3-gabc1234 between
# releases), "" if git cannot tell. Sourced by the scripts that run on the
# host (ci/run.sh, ci/host/*.sh); they pass it to the build as
# LWEXT4_VERSION, since a container cannot run git in a worktree (its git
# directory is not in the mount).
#
# The checkouts of GitHub Actions are shallow, without tags: there the
# history and the tags are fetched first, so that CI builds report the
# version of their tag (CMake stops if they do not).
ci_version()
{
	if [ -n "${GITHUB_ACTIONS:-}" ] &&
	   [ "$(git -C "$1" rev-parse --is-shallow-repository)" = true ]; then
		git -C "$1" fetch --quiet --unshallow --tags origin >&2 ||
			echo "ci_version: cannot fetch the history and tags" >&2
	fi
	v=$(git -C "$1" describe --tags --match 'v[0-9]*' --dirty 2>/dev/null) ||
		v=
	echo "${v#v}"
}
