Fuzz testing
============

libFuzzer targets, built with clang, AddressSanitizer and
UndefinedBehaviorSanitizer, with leak checks:

| Target | Input | What it does |
|---|---|---|
| `fuzz_mount.c` | a disk image | mounts it read only and reads everything: directories, files, symlinks, xattr lists |
| `fuzz_rw.c` | a disk image followed by an operation script (layout in the file) | mounts it read-write, replays the journal, runs up to 64 operations (create, write anywhere, truncate, rename, unlink, mkdir/rmdir, xattrs, symlinks, links, many entries to grow htrees, write-back cache), unmounts, then mounts the result read only and reads it all back |
| `fuzz_rwx.c` | a disk image, an operation script, and a fault: which reads or writes fail, from the Nth on, once or for good | `fuzz_rw` with I/O errors and more operations: remounting in the middle, renames across types, readlink at offsets, xattr reads, 64 KiB writes, links to directories, growing truncates, cache flushes, listings with a rewind, tree removal, FIFOs, the journal stopped and started; then, with the device working again, everything must be readable |
| `fuzz_partition.c` | a disk | scans its partition tables (MBR with logical partitions, GPT, the four entry MBR) and mounts the first partition read only |
| `fuzz_mkfs.c` | the parameters of `ext4_mkfs` (layout in the file) | formats a RAM disk with them (size, block size, ext2/3/4, journal size, i-node size and count, group sizes, feature words, label), mounts it read-write and writes to it, reads it back read only, and reads the parameters back with `ext4_mkfs_read_info` |

`fuzz_common.h` has the RAM block device over a copy of the input, with
fault injection, and the bounded tree walk; at the end of every input it
unmounts whatever is still mounted and stops if that fails, so no state
passes to the next input. `fuzz_ops.h` has the operations `fuzz_rw` and
`fuzz_rwx` share. [ext4.dict](ext4.dict) gives libFuzzer the magic
numbers and names of the format.

Inputs
------

- **Seeds** are made when the job runs by [make-seeds.sh](make-seeds.sh)
  with e2fsprogs: ext2, ext3 and ext4 layouts (block maps, journal,
  extents, metadata_csum, htree directories, in-inode and block xattrs,
  POSIX ACL, fast and slow symlinks, 1, 2 and 4 KiB blocks, meta_bg,
  large_dir, 128 byte i-nodes, deep extent trees, journals with
  transactions to replay), MBR and GPT disks for `fuzz_partition`, and
  parameters for `fuzz_mkfs`; each image with three operation scripts for
  `fuzz_rw` and two with faults for `fuzz_rwx`. Fixed UUIDs, hash seeds,
  times and partition ids make them reproducible.
- **[crashes/](crashes)**: every input that ever crashed, hung or leaked in
  a target, minimised, kept as a regression test.

Running
-------

```sh
ci/run.sh fuzz replay             # every seed and crash input once (CI job fuzz-replay)
ci/run.sh fuzz run 600            # fuzz each target for 600 s
ci/run.sh fuzz run 3600 fuzz_rw   # one target
```

New crash inputs land in `build-ci/fuzz/art/`, and the full output of each
target in `build-ci/fuzz/<target>.run.log`. The corpus a run grows is kept
in `build-ci/fuzz-corpus/<target>`, so the next run continues from it. To
report a crash: minimise the input
(`build-ci/fuzz/<target> -minimize_crash=1 -runs=10000 <input>`), file an
issue, and add the input, gzipped (`gzip -9 -n`), to `crashes/` in the
pull request that fixes it, so `fuzz-replay` keeps it fixed.

Every night
-----------

[fuzz.yml](../../.github/workflows/fuzz.yml) fuzzes every target on GitHub
for 30 minutes each, from the corpus of the nights before (kept in the
Actions cache). When a target crashes, hangs or leaks, its job fails,
uploads the inputs and the target's output, and opens an issue labelled
[`fuzzing`](https://github.com/antimatter-studios/lwext4/issues?q=label%3Afuzzing)
with the report and the commands to reproduce it, or comments on the one
already open for that target. It runs by hand too, for any time per
target: `gh workflow run fuzz.yml -f seconds=3600`.
