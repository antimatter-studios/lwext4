> **antimatter-studios/lwext4**: the maintained fork of
> [gkostka/lwext4](https://github.com/gkostka/lwext4), which has merged
> nothing since 2022. Development, issues and releases are here
> ([CONTRIBUTING.md](CONTRIBUTING.md)).
>
> - **Docs:** the [project site](https://antimatter-studios.github.io/lwext4/):
>   this README, the API reference, the build options, every regression
>   test and fuzzer crash input with its issue, the CI jobs, benchmarks.
> - **Releases:** `v1.0.1-am.<n>` tags on `main`. A tag runs the whole CI
>   matrix and publishes, only if every job passes: libraries for Linux
>   (9 architectures), Windows and Cortex-M, the example firmware, test
>   results and the list of changes.
> - **Requests, bugs, questions:** [open an issue](https://github.com/antimatter-studios/lwext4/issues).

[![CI](https://github.com/antimatter-studios/lwext4/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/antimatter-studios/lwext4/actions/workflows/ci.yml?query=branch%3Amain)
[![Release](https://img.shields.io/github/v/release/antimatter-studios/lwext4?include_prereleases)](https://github.com/antimatter-studios/lwext4/releases)
[![License: BSD-3-Clause, GPL-2.0 (ext4_extent.c, ext4_xattr.c)](https://img.shields.io/badge/license-BSD--3--Clause%20%2F%20GPL--2.0-blue.svg)](#credits)

![lwext4](https://cloud.githubusercontent.com/assets/8606098/11697327/68306d88-9eb9-11e5-8807-81a2887f077e.png)

About
=====

ext2/3/4 for microcontrollers, an alternative to the usual FAT libraries:

- directory indexing - fast file find and list operations
- extents - fast big file truncate
- journaling transactions & recovery - power loss resistance

For SD/MMC cards, USB flash drives and other wear levelled storage; not
for raw flash.

Getting started
=====

lwext4 is a library: your program registers a *block device* (a struct
with read/write callbacks for your storage), formats or mounts it, and
uses a file API much like stdio. Everything below runs on a PC first.

1. Build the library, the tools and the examples ([dependencies](#compile)):
   ```bash
   make generic
   cd build_generic
   make
   ```
2. Run the basic example and check its result with e2fsprogs:
   ```bash
   ./examples/lwext4-example-basic disk.img
   e2fsck -fn disk.img
   debugfs -R 'ls -l /docs' disk.img
   ```
3. Port it to your hardware: write a block device, starting from
   [examples/blockdev-template](examples/blockdev-template/my_blockdev.c).
4. Pick features and buffer sizes with the `CONFIG_*` options of
   [include/ext4_config.h](include/ext4_config.h), and build for your
   target ([Build for a microcontroller](#build-for-a-microcontroller)).

The core of a program (CI compiles this snippet, like every C snippet in
this file):
```c
#include <ext4.h>
#include <ext4_mkfs.h>
#include <blockdev/file_dev.h>

/* Format an image file as ext4, then write "hello" to /mp/hello.txt. */
int format_and_write(const char *image)
{
	static struct ext4_fs fs; /* used by ext4_mkfs only; large */
	struct ext4_mkfs_info info = {.block_size = 1024, .journal = true};
	struct ext4_blockdev *bd;
	ext4_file f;
	size_t written;
	int r;

	file_dev_name_set(image);  /* the block device: a file */
	bd = file_dev_get();

	r = ext4_mkfs(&fs, bd, &info, F_SET_EXT4);
	if (r != EOK)
		return r;

	r = ext4_device_register(bd, "disk");
	if (r != EOK)
		return r;
	r = ext4_mount("disk", "/mp/", false);
	if (r == EOK) {
		ext4_recover("/mp/");       /* replay the journal if needed */
		ext4_journal_start("/mp/");

		r = ext4_fopen(&f, "/mp/hello.txt", "wb");
		if (r == EOK) {
			r = ext4_fwrite(&f, "hello\n", 6, &written);
			ext4_fclose(&f);
		}

		ext4_journal_stop("/mp/");
		ext4_umount("/mp/");
	}
	ext4_device_unregister("disk");
	return r;
}
```

Examples
-----

Each one is built and run by CI, and what it writes is checked with
`e2fsck -fn` and `debugfs` ([examples/](examples/README.md)).

| Example | Runs on | What it shows |
|---|---|---|
| [basic](examples/basic/main.c) | PC | the whole life cycle, step by step: mkfs, mount, journal, cache, files and directories, unmount |
| [blockdev-template](examples/blockdev-template/my_blockdev.c) | PC (RAM disk) | an annotated block device skeleton: the part you write for new hardware |
| [baremetal-sdcard](examples/baremetal-sdcard/README.md) | NUCLEO-F401RE, NUCLEO-G071RB, NUCLEO-L552ZE-Q, nRF52840 DK, in Renode | bare metal, micro SD card on SPI, no SDK or RTOS: MBR, mkfs, journal, power cuts |
| [zephyr](examples/zephyr/README.md) | Zephyr on mps2/an385, in QEMU | lwext4 as a Zephyr module on the disk access API |
| [esp-idf](https://github.com/antimatter-studios/lwext4/tree/main/examples/esp-idf) | ESP32, ESP32-C3, ESP32-S3, in Espressif's QEMU | an ESP-IDF component with SPI flash and SD card block devices |

The tools of [fs_test/](fs_test) (`lwext4-generic`, `lwext4-mkfs`,
`lwext4-mbr`) are complete programs too.

Features
=====

* filetypes: regular, directories, softlinks
* support for hardlinks
* extended attributes (POSIX ACLs are kept as xattrs)
* timestamps from whatever clock the board has: a real-time clock, a start
  date advanced by an uptime counter, or, with no clock at all, the newest
  time stored on the filesystem (`ext4_clock_setup` in `ext4.h`)
* multiple blocksize supported: 1KB, 2KB, 4KB ... 64KB
* little/big endian architectures supported
* multiple configurations (ext2/ext3/ext4)
* only C standard library dependency
* various CPU architectures supported (x86/64, cortex-mX, msp430 ...)
* small memory footprint
* flexible configurations
* partition tables: MBR including logical partitions, and GPT
  (`ext4_partition_scan` in `ext4_partition.h`)

Memory footprint
------------

The memory hungry features (journal, extents, xattrs) can be left out at
compile time. Cortex-M4, arm-none-eabi-gcc 14, -O2, debug output off
(measured by tests/acceptance/test-cortex-m.sh):

* .text:  60KB - only ext2 fs support , 78KB - full ext4 fs feature set
* RAM:    15KB - minimum 8 x 1KB  block cache (10KB heap, 5KB static data), 24KB - when journaling and extents are enabled
* .stack: 2KB - is enough (about 1KB measured)

Per operation, on emulated Cortex-M boards, measured on every pull request
([how](docs/performance/README.md); generated by `ci/run.sh bench update`,
do not edit):

<!-- bench-table: begin (tests/bench/table.py) -->
| | cortex-m0 | cortex-m3 | cortex-m4 | cortex-m7 |
|---|---|---|---|---|
| Flash (whole library) | 100.3 KiB | 78.5 KiB | 78.3 KiB | 78.7 KiB |
| Static RAM (data + bss) | 4.9 KiB | 4.9 KiB | 4.9 KiB | 4.9 KiB |
| Peak heap | 9.2 KiB | 9.2 KiB | 9.2 KiB | 9.2 KiB |
| Peak stack | 1.0 KiB | 0.9 KiB | 0.9 KiB | 0.9 KiB |

| Operation | Block reads | Block writes | cortex-m0 instructions | cortex-m3 instructions | cortex-m4 instructions | cortex-m7 instructions |
|---|---|---|---|---|---|---|
| mkfs | 11 | 216 | 8351 k | 3824 k | 3821 k | 3840 k |
| mount | 5 | 3 | 23 k | 12 k | 12 k | 12 k |
| create | 5 | 12 | 65 k | 37 k | 36 k | 36 k |
| write-4k | 3 | 23 | 124 k | 67 k | 66 k | 66 k |
| write-1m | 10 | 2828 | 15933 k | 8160 k | 8025 k | 8038 k |
| read-1m | 260 | 0 | 2816 k | 1439 k | 1408 k | 1407 k |
| lookup-x100 | 694 | 0 | 3416 k | 1926 k | 1906 k | 1911 k |
| truncate-1m | 7 | 10 | 121 k | 74 k | 73 k | 73 k |
| unlink | 19 | 14 | 124 k | 69 k | 68 k | 68 k |
| umount | 0 | 3 | 8 k | 5 k | 5 k | 5 k |
<!-- bench-table: end -->

The library allocates its blocks with `malloc`, never more than
`CONFIG_BLOCK_DEV_CACHE_SIZE` cache blocks. `CONFIG_USE_USER_MALLOC=1`
calls `ext4_user_malloc`, `_calloc`, `_realloc` and `_free` instead, e.g.
for a fixed pool. `tests/test_memory.c` replays the allocations of
typical workloads through a first-fit allocator on a fixed area, as a
microcontroller heap, and fails on a leak or above the ceilings of
`tests/test_memory_budget.h`: with 16 cache buffers the busiest workload
needs a 24KB heap with 1KB blocks, 87KB with 4KB blocks.

Supported ext2/3/4 features
=====

<!-- features: begin (checked by tests/test_features.c) -->
Every row below is a claim that CI proves on every pull request:
[`tests/test_features.sh`](tests/test_features.sh) reads these tables,
makes a filesystem for each row with e2fsprogs (`mke2fs`, or `debugfs` to
set a flag `mke2fs` will not), and
[`tests/test_features.c`](tests/test_features.c) checks what lwext4 does
with it:

- **read-write**: it mounts read-write; creating, writing, renaming,
  linking, truncating and deleting files and directories (with xattrs and
  a 300 entry directory) through the journal leaves a filesystem that
  `e2fsck -fn` finds clean, and everything reads back after a remount.
- **read-only**: it mounts, reads, refuses writes with `EROFS`, and leaves
  the image unchanged.
- **refused**: mounting fails with `ENOTSUP`.

| Filesystem (mke2fs defaults) | Block size | lwext4 |
|---|---|---|
| `ext2` | 1 KiB | **read-write** |
| `ext2` | 4 KiB | **read-write** |
| `ext3` | 1 KiB | **read-write** |
| `ext3` | 4 KiB | **read-write** |
| `ext4` | 1 KiB | **read-write** |
| `ext4` | 2 KiB | **read-write** |
| `ext4` | 4 KiB | **read-write** |
| `ext4` | 64 KiB | **read-write** |

The defaults of e2fsprogs 1.47 include metadata_csum_seed and orphan_file.

Features, each added to (or, for `meta_bg`, replacing `resize_inode` in)
an ext4 with 1 KiB blocks:

| Feature | Kind | lwext4 | Notes |
|---|---|---|---|
| `has_journal` | compatible | **read-write** | replayed by `ext4_recover`, written between `ext4_journal_start` and `ext4_journal_stop` |
| `ext_attr` | compatible | **read-write** | in-inode and block xattrs, POSIX ACLs kept as xattrs |
| `dir_index` | compatible | **read-write** | htree directories, read and written |
| `resize_inode` | compatible | **read-write** | the reserved descriptor blocks are kept; lwext4 does not resize |
| `sparse_super2` | compatible | **read-write** | |
| `fast_commit` | compatible | **read-write** | lwext4 writes full commits only |
| `stable_inodes` | compatible | **read-write** | lwext4 never renumbers i-nodes |
| `orphan_file` | compatible | **read-write** | see orphan_present |
| `dir_prealloc` | compatible | **read-write** | ignored, as by Linux |
| `imagic_inodes` | compatible | **read-write** | ignored, as by Linux |
| `sparse_super` | read-only compatible | **read-write** | |
| `large_file` | read-only compatible | **read-write** | |
| `huge_file` | read-only compatible | **read-write** | |
| `uninit_bg` | read-only compatible | **read-write** | group descriptor checksums (gdt_csum), uninitialised groups |
| `dir_nlink` | read-only compatible | **read-write** | |
| `extra_isize` | read-only compatible | **read-write** | |
| `metadata_csum` | read-only compatible | **read-write** | every checksum verified and written |
| `quota` | read-only compatible | **read-only** | |
| `bigalloc` | read-only compatible | **read-only** | also with 1 KiB blocks, whose descriptors follow the superblock in block 1 (fork issue #171) |
| `project` | read-only compatible | **read-only** | |
| `verity` | read-only compatible | **read-only** | |
| `replica` | read-only compatible | **read-only** | |
| `read-only` | read-only compatible | **read-only** | |
| `shared_blocks` | read-only compatible | **read-only** | |
| `orphan_present` | read-only compatible | **read-only** | orphans pending in the orphan file; without it, orphans of the old list are released at a read-write mount, as Linux does |
| `filetype` | incompatible | **read-write** | |
| `extent` | incompatible | **read-write** | |
| `flex_bg` | incompatible | **read-write** | |
| `64bit` | incompatible | **read-write** | |
| `meta_bg` | incompatible | **read-write** | |
| `metadata_csum_seed` | incompatible | **read-write** | |
| `large_dir` | incompatible | **read-write** | three level htrees, directories over 2 GiB |
| `inline_data` | incompatible | **read-write** | inline files and directories are read, and moved to a block before they change (as Linux does when they no longer fit); files lwext4 creates are not inline. Writing needs the xattr code: without it (`CONFIG_XATTR_ENABLE=0`) such a filesystem is mounted read-only |
| `needs_recovery` | incompatible | **read-write** | after `ext4_recover` replays the journal |
| `mmp` | incompatible | **read-write** | multi-mount protection is not implemented: mounted without it |
| `compression` | incompatible | **refused** | |
| `journal_dev` | incompatible | **refused** | an external journal device, not a filesystem |
| `ea_inode` | incompatible | **refused** | |
| `dirdata` | incompatible | **refused** | |
| `encrypt` | incompatible | **refused** | |
| `casefold` | incompatible | **refused** | |
<!-- features: end -->

Not listed: `lazy_bg`, an obsolete flag that e2fsck itself refuses, so
nothing can show that a filesystem with it is right; lwext4 ignores it,
as Linux does.

A feature lwext4 does not know is treated by its kind: a compatible one is
ignored, a read-only compatible one makes the mount read-only, an
incompatible one refuses it. The feature level of the build
(`CONFIG_EXT_FEATURE_SET_LVL`, see [the configuration](include/ext4_config.h))
narrows the list (an ext2 build has no journal, extents or checksums);
the tables are checked with the default, ext4.

How it is tested
=====

On every pull request, and nothing is allowed to fail. Every job runs in a
container of [ci/](ci/README.md), so `ci/run.sh <job>` runs it locally
with only docker.

| What | How | Job |
|---|---|---|
| Regression tests | the CTest suite in [tests/](tests); every fixed bug has a test, which must fail without the fix and pass with it | `native`, `red-green` |
| Sanitizers | gcc and clang with ASan + UBSan, TSan | `native asan-ubsan` |
| e2fsprogs as the oracle | what lwext4 writes must pass `e2fsck -fn` and read back with `debugfs`, and the reverse; every claim of this README is checked ([tests/acceptance](tests/acceptance/README.md)) | README acceptance |
| Power loss | a power cut after every single block write of a workload, then journal replay and `e2fsck` | `readme-api` |
| Linux architectures | x86_64, and under qemu-user aarch64, armhf, i686, mips, powerpc, ppc64le, riscv64, s390x (big endian); macOS; Windows under Wine | `qemu-user <arch>`, `mingw` |
| Microcontrollers | [tests/baremetal](tests/baremetal) on Cortex-M0, M0+, M3, M4, M4F, M7 (QEMU), ARM7TDMI (qemu-arm), ATmega1284 (simavr), MSP430X (GDB simulator) | `cortex-m <cpu>`, `arm-sim`, `avr`, `msp430` |
| Example firmware | the [examples](#examples) in Renode, QEMU and Espressif's QEMU | own workflows |
| Fuzzing | libFuzzer targets for mount, read-write, I/O errors, partitions and mkfs; every crash input is replayed on every pull request, and the targets fuzz for 30 minutes each night ([tests/fuzz](tests/fuzz/README.md)) | `fuzz replay`, nightly: Fuzzing workflow |
| Coverage | line and branch coverage may only go up | `coverage` |
| Cost | instructions, block I/O, heap and stack per operation; more than 5 % worse fails | `bench` |
| Embedded budget | the C library functions the Cortex-M0 build calls, and its largest stack frame | `embedded-budget` |

Project tree
=====
*  blockdev         - block devices: image files, Linux and Windows devices
*  examples         - example programs and firmware (see [Examples](#examples))
*  fs_test          - the lwext4-generic, lwext4-mkfs and lwext4-mbr tools, and the client/server test suite
*  src              - source files
*  include          - header files
*  ports            - glue for RTOSes and SDKs (Zephyr)
*  platforms        - board code of the test firmware and the examples
*  tests            - regression tests, acceptance tests, fuzzing, benchmark, test firmware
*  toolchain        - cmake toolchain files
*  ci               - CI jobs and their containers (see [ci/README.md](ci/README.md))
*  docs             - the project site and the benchmark data
*  CMakeLists.txt   - CMake config file
*  fs_test.mk       - automatic tests definitions
*  Makefile         - helper makefile to generate cmake and run test suite
*  README.md        - readme file

Compile
=====
Dependencies
------------
* Windows: [MSYS2](https://www.msys2.org/), then in its shell:
```bash
pacman -S make gcc cmake
```
* Linux (Debian):
```bash
apt-get install make gcc cmake
```

Compile & install tools
------------
```bash
make generic
cd build_generic
make
sudo make install
```

Installation goes to /usr/local by default. Another prefix, at install
time or when configuring:
```bash
cmake --install build_generic --prefix $HOME/.local
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local build_generic
```

Using the installed library
------------
`make install` installs the library, the file block device library, their
headers, a pkg-config file and a CMake package:
```
include/lwext4/                 ext4.h, ext4_mkfs.h, ... generated/ext4_config.h
include/lwext4/blockdev/        blockdev.h, file_dev.h (file_windows.h on Windows)
lib/liblwext4.a                 the library (liblwext4.so with -DLWEXT4_BUILD_SHARED_LIB=ON)
lib/libblockdev.a               file_dev_get(): an image file or device as ext4_blockdev
lib/pkgconfig/lwext4.pc
lib/cmake/lwext4/               lwext4Config.cmake, lwext4ConfigVersion.cmake, targets
```
Both find the prefix relative to their own location, so an install tree
can be moved. A program includes `<ext4.h>` (and `<blockdev/file_dev.h>`)
and is built with any of:
```bash
cc app.c $(pkg-config --cflags --libs lwext4)
cc app.c -I$PREFIX/include/lwext4 -L$PREFIX/lib -lblockdev -llwext4
```
```cmake
find_package(lwext4 1.0 CONFIG REQUIRED)
target_link_libraries(app lwext4::blockdev lwext4::lwext4)
```
(with `PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig` or `CMAKE_PREFIX_PATH=$PREFIX`
for a prefix outside the default search path). The `install_package` CTest
test builds and runs such a program all three ways; `tests/package/` is a
complete example.

Build for a microcontroller
=====

`make <toolchain>` configures `build_<toolchain>` with
`toolchain/<toolchain>.cmake`; `make lwext4` there builds the library:
```bash
make cortex-m4
cd build_cortex-m4
make lwext4
```

| Toolchain (`make ...`) | Compiler | Tested |
|---|---|---|
| `cortex-m0`, `cortex-m0+`, `cortex-m3`, `cortex-m4`, `cortex-m4f`, `cortex-m7` | arm-none-eabi-gcc | tests/baremetal on QEMU MPS2 boards |
| `arm-sim` | arm-none-eabi-gcc | tests/baremetal on qemu-arm (ARM7TDMI) |
| `avrxmega7` | avr-gcc | compiled only (simavr has no XMEGA; ATmega1284 runs the tests) |
| `msp430` | msp430-elf-gcc or msp430-gcc | compiled only (MSP430G2210; MSP430X large model runs the tests) |
| `mingw` | MinGW-w64 | CTest suite under Wine |
| `generic` | the host's | all of [How it is tested](#how-it-is-tested) |

Other Linux architectures use `toolchain/linux-cross.cmake` (see [Run
regression tests](#run-regression-tests)). The byte order comes from the
compiler; `CONFIG_BIG_ENDIAN` (1 or 0) overrides it for compilers that
do not say, and contradicting the compiler is a build error.

lwext4-generic demo application
=====
Mounts ext2/3/4 images, Linux block devices or Windows volumes, and runs
directory and file read/write speed tests.
`make test` or `make images_small` creates the ext_images directory:
```bash
lwext4-generic -i ext_images/ext2
lwext4-generic -i ext_images/ext3
lwext4-generic -i ext_images/ext4
```
All options:
```bash
lwext4-generic --help
```

Run automatic tests
=====

The client/server suite of fs_test, on images it makes with `mke2fs`
(e2fsprogs), no root needed. 128MB images:
```bash
make test
```
1GB images (Linux only), then fsck:
```bash
make test_all
```

Run regression tests
=====

The CTest suite in `tests/`. It needs `mke2fs` (e2fsprogs), `sfdisk`
(util-linux) and `python3` to build its images, and `pkg-config` for
`install_package`; no root:
```bash
make generic
cd build_generic
make
ctest --output-on-failure
```
With AddressSanitizer/UBSan: configure with
`-DLWEXT4_SANITIZE=address,undefined`.

Other Linux architectures under qemu-user, with `toolchain/linux-cross.cmake`
(`gcc-<triple>` and `qemu-user` packages):
```bash
cmake -S . -B build_s390x -DCMAKE_TOOLCHAIN_FILE=toolchain/linux-cross.cmake \
      -DCROSS_TRIPLE=s390x-linux-gnu -DCROSS_EMULATOR=qemu-s390x
cmake --build build_s390x
ctest --test-dir build_s390x --output-on-failure
ci/scripts/fs-roundtrip.sh build_s390x qemu-s390x -L /usr/s390x-linux-gnu
```
`fs-roundtrip.sh` formats images with mke2fs and lwext4-mkfs, exercises
them with lwext4-generic and checks them with e2fsck.

The microcontroller toolchains also build [tests/baremetal](tests/baremetal),
a test firmware that `ctest` runs in an emulator (see [Build for a
microcontroller](#build-for-a-microcontroller)).

Every CI job, locally, with only docker ([ci/README.md](ci/README.md)):
```bash
ci/run.sh --list
ci/run.sh qemu-user s390x
ci/run.sh avr
```

Using lwext4-mkfs tool
=====
Formats an image or device as ext2/3/4. A 1GB image:
```bash
dd if=/dev/zero of=ext_image bs=1M count=1024
```
ext2:
```bash
lwext4-mkfs -i ext_image -e 2
```
ext3:
```bash
lwext4-mkfs -i ext_image -e 3
```
ext4:
```bash
lwext4-mkfs -i ext_image -e 4
```
All options:
```bash
lwext4-mkfs --help
```

Credits
=====

Written by Grzegorz Kostka. Most of the source code comes from
[HelenOS](http://helenos.org/); some features are based on FreeBSD and
Linux.

KaHo Ng (https://github.com/ngkaho1234): advanced extents, xattrs,
metadata checksums, journal recovery and transactions, many fixes.
[fuse-lwext4](https://github.com/ngkaho1234/fuse-lwext4) uses lwext4 as a
FUSE filesystem.

Some of the source files are licensed under GPLv2. It makes whole
lwext4 GPLv2 licensed. To use library as a BSD3, GPLv2 licensed source
files must be removed first. At this point there are two files
licensed under GPLv2:
* ext4_xattr.c
* ext4_extent.c

All other modules and headers are BSD-3-Clause licensed code.
