#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
#
# Rebuild the `integration` branch: upstream master plus every topic branch
# of ci/integration/branches.txt merged in order. Any merge conflict stops
# the rebuild with an error (the branches must then be rebased/coordinated).
#
#   ci/integration/rebuild.sh [--push | --check] [remote]
#
# remote (default origin) is where the topic branches are fetched from and,
# with --push, where `integration` is force-pushed to. --check instead fails
# unless remote's `integration` is exactly the rebuild (merges are
# reproducible, so an up to date integration has the same commit). Upstream master comes
# from UPSTREAM_URL (default https://github.com/gkostka/lwext4.git); set
# UPSTREAM_BASE=<commit> to use a fixed commit instead.
#
# The local branch `integration` is updated, and build-ci/integration/
# manifest.md lists the upstream base and the commit of every branch.
set -eu

mode=
case "${1:-}" in
--push|--check) mode=$1; shift ;;
esac
remote=${1:-origin}
list=ci/integration/branches.txt
out=build-ci/integration
upstream_url=${UPSTREAM_URL:-https://github.com/gkostka/lwext4.git}

branches=$(sed 's/#.*//' "$list" | awk 'NF { print $1 }')
[ -n "$branches" ] || { echo "no branches in $list" >&2; exit 1; }

git fetch --quiet "$remote" $(for b in $branches; do
	printf '+refs/heads/%s:refs/remotes/%s/%s ' "$b" "$remote" "$b"; done)

if [ -n "${UPSTREAM_BASE:-}" ]; then
	base=$(git rev-parse --verify "$UPSTREAM_BASE^{commit}")
else
	git fetch --quiet "$upstream_url" master
	base=$(git rev-parse FETCH_HEAD)
fi

rm -rf "$out"
mkdir -p "$out"
wt=$out/worktree
git worktree prune
git worktree add --quiet --detach "$wt" "$base"

{
	echo "# lwext4 integration manifest"
	echo
	echo "Upstream base: gkostka/lwext4 master $(git log -1 --format='%H (%cs) %s' "$base")"
	echo
	echo "| Branch | Commit | Subject |"
	echo "|---|---|---|"
} >"$out/manifest.md"

for b in $branches; do
	ref=$remote/$b
	sha=$(git rev-parse --verify "$ref^{commit}")
	echo "merging $b ($(git log -1 --format=%h "$sha"))"
	# Reproducible merge commits: fixed identity, and the later commit
	# date of the two parents. The same inputs give the same integration
	# commit, so "is integration up to date" is a commit comparison.
	t1=$(git log -1 --format=%ct "$sha")
	t2=$(git -C "$wt" log -1 --format=%ct HEAD)
	[ "$t1" -gt "$t2" ] || t1=$t2
	if ! GIT_AUTHOR_DATE="@$t1 +0000" GIT_COMMITTER_DATE="@$t1 +0000" \
	    git -C "$wt" -c user.name="lwext4 integration" \
	    -c user.email="integration@invalid" \
	    merge --quiet --no-ff --no-edit -m "Merge branch '$b'" "$sha"; then
		echo "CONFLICT merging $b:" >&2
		git -C "$wt" diff --name-only --diff-filter=U >&2
		git -C "$wt" merge --abort
		git worktree remove --force "$wt"
		exit 1
	fi
	echo "| $b | $(git log -1 --format=%h "$sha") | $(git log -1 --format=%s "$sha") |" \
		>>"$out/manifest.md"
done

head=$(git -C "$wt" rev-parse HEAD)
git worktree remove --force "$wt"
if [ "$(git symbolic-ref -q --short HEAD || true)" = integration ]; then
	git checkout --quiet --detach # keep the files, free the branch
fi
git branch -f integration "$head"
echo "integration = $head"
cat "$out/manifest.md"

if [ "$mode" = --push ]; then
	git push --force "$remote" integration
elif [ "$mode" = --check ]; then
	git fetch --quiet "$remote" \
		"+refs/heads/integration:refs/remotes/$remote/integration" || true
	current=$(git rev-parse -q --verify "$remote/integration" || true)
	if [ "$current" != "$head" ]; then
		echo "$remote/integration (${current:-missing}) is not the rebuild" \
			"($head): run ci/integration/rebuild.sh --push" >&2
		exit 1
	fi
	echo "$remote/integration is up to date"
fi
