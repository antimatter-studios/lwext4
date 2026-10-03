Fuzz testing
============

libFuzzer targets, built with clang, AddressSanitizer and
UndefinedBehaviorSanitizer, with leak checks:

| Target | Input | What it does |
|---|---|---|
| `fuzz_mount.c` | a disk image | mounts it read only and reads everything: directories, files, symlinks, xattr lists |
| `fuzz_rw.c` | a disk image followed by an operation script (layout in the file) | mounts it read-write, replays the journal, runs up to 64 operations (create, write anywhere, truncate, rename, unlink, mkdir/rmdir, xattrs, symlinks, links, many entries to grow htrees, write-back cache), unmounts, then mounts the result read only and reads it all back |

`fuzz_common.h` has the RAM block device over a copy of the input and the
bounded tree walk.

Inputs
------

- **Seeds** are made when the job runs by [make-seeds.sh](make-seeds.sh)
  with e2fsprogs: ext2, ext3 and ext4 layouts (block maps, journal,
  extents, metadata_csum, htree directories, in-inode and block xattrs,
  POSIX ACL, fast and slow symlinks, 1 KiB and 2 KiB blocks), each with
  three operation scripts appended for `fuzz_rw`. Fixed UUID, hash seed and
  times make them reproducible.
- **[crashes/](crashes)**: every input that ever crashed, hung or leaked in
  a target, minimised, kept as a regression test.

Running
-------

```sh
ci/run.sh fuzz replay             # every seed and crash input once (CI job fuzz-replay)
ci/run.sh fuzz run 600            # fuzz each target for 600 s
ci/run.sh fuzz run 3600 fuzz_rw   # one target
```

New crash inputs land in `build-ci/fuzz/art/`. To report one: minimise it
(`build-ci/fuzz/<target> -minimize_crash=1 -runs=10000 <input>`), file an
issue, and add the minimised input to `crashes/` in the pull request that
fixes it, so `fuzz-replay` keeps it fixed.
