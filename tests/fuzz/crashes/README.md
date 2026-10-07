Inputs that crashed a fuzz target
=================================

Every input here once crashed, hung or leaked in a target of
[tests/fuzz](..). The CI job `fuzz-replay` runs each through every target
on every pull request, so the bug stays fixed. They are gzipped (`gzip -9
-n`; mostly zeros of disk images); name new ones `<issue>-<what>.gz`.

| Input | Target | Bug |
|---|---|---|
| `134-listxattr-unknown-index.gz` | fuzz_mount | #134: ext4_listxattr passed the NULL prefix of an unknown xattr name index to memcpy |
| `135-journal-stop-without-start.gz` | fuzz_rw | #135: ext4_journal_stop after a failed ext4_journal_start dereferenced a NULL journal |
| `142-balloc-metadata.gz` | fuzz_rw | #142: the block allocator handed out metadata (block 0) that a damaged bitmap marked free |
| `146-journal-hole.gz` | fuzz_rw | #146: a hole in the journal inode was used as block 0 |
| `147-dir-holes-slow.gz` | fuzz_mount | #147: listing a directory with a huge hole took seconds (a timeout) |
| `147-dir-lookup-holes-slow.gz` | fuzz_rw | #147: looking a name up in such a directory, likewise |
| `147-dir-bmap-holes-slow.gz` | fuzz_rw | #147: the same in a block mapped directory |
| `147-dir-add-holes-slow.gz` | fuzz_rw | #147: adding an entry to such a directory |
| `150-dir-rm-cycle.gz` | fuzz_rw | #150: ext4_dir_rm descended a damaged tree with a cycle forever (a timeout) |
| `152-truncate-huge-size.gz` | fuzz_rw | #152: truncating an orphan with a damaged size of 2^62 never ended (a timeout) |
| `155-dx-csum-count.gz` | fuzz_mount | #155: the htree checksum read past the block for an index node whose count exceeds its limit |
| `157-journal-block-size.gz` | fuzz_rw | #157: a journal block size of 64 KiB on a 1 KiB filesystem overflowed block buffers |
| `160-bg-gdt-overlap.gz` | fuzz_mount | #160: a block bitmap on the group descriptors wiped them (assert on block 0; a read-only mount wrote them back) |
| `162-truncate-inline-huge.gz` | fuzz_rw | #162: truncating an inline file with a damaged size of about 2^64 never ended (a timeout) |
| `174-mkfs-tiny-journal.gz` | fuzz_mkfs | #174: ext4_mkfs made a 9 block journal, and a transaction bigger than the journal hit an assert |
| `179-inode-128-copy.gz` | fuzz_mkfs | #179: with 128 byte i-nodes, copying the journal's i-node read past the cache buffer |
| `199-free-blocks-beyond-fs.gz` | fuzz_rw | #199: freeing an extent that starts at block 2^48-1 of a 128 block filesystem wrapped the block group indexes, freed nothing and hit `ext4_assert(count == 0)` |
| `mount-before-2026-10-04-ce41bd70.gz` | fuzz_mount | found on 2026-10-01 by the harness before it was in the repository; fixed on main by 2026-10-04 (not bisected) |

Some crashes need the state an earlier input left behind and do not
reproduce alone, so they have no input here; their regression tests are
in `tests/`: #167 (a journal session survived `ext4_umount`,
`test_umount_journal_session`), #177 (`ext4_journal_start` with a session
open, `test_journal_start_twice`) and #181 (a failed `ext4_umount`,
`test_umount_failed`). Since then every target unmounts at the end of an
input and stops if that fails (`unmount_all()` in `fuzz_common.h`), so
such state can no longer pass from one input to the next.
