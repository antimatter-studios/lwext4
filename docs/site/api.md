# lwext4 API reference {#mainpage}

Generated from the comments in the public headers (`include/`) of the
version this site was built from.

- **ext4.h** is the API an application uses: block devices
  (`ext4_device_register`), mounting (`ext4_mount`, `ext4_recover`,
  `ext4_journal_start`), files (`ext4_fopen`, `ext4_fread`, `ext4_fwrite`,
  ...), directories, links, extended attributes, the clock
  (`ext4_clock_setup`) and the cache.
- **ext4_blockdev.h** is what a port implements: the block device
  interface (open, read and write blocks, close, optional locking).
- **ext4_mkfs.h** formats a block device; **ext4_partition.h** and
  **ext4_mbr.h** find and write MBR and GPT partitions.
- **ext4_config.h** has the build options (see the site's Configuration
  page).

The other headers are the library's internals, documented for those who
work on it.
