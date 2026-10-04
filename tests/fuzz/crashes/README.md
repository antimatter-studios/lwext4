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
| `mount-before-2026-10-04-ce41bd70.gz` | fuzz_mount | found on 2026-10-01 by the harness before it was in the repository; fixed on main by 2026-10-04 (not bisected) |
