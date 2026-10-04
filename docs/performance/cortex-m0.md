# Performance on cortex-m0

These figures come from emulators: nobody has every board, so the
benchmark (tests/bench) runs on QEMU's MPS2 boards in CI. Instruction
counts, code and data sizes, heap, stack and block I/O are exact for the
code as built (arm-none-eabi-gcc -O2, see the toolchain files); real
hardware adds what an emulator does not model (caches, flash wait states,
the storage). Read them as a ball-park guide: at a clock of f MHz, n
million instructions are roughly n / f seconds of CPU, and on an MCU with
an SD card the block writes usually cost more than the CPU.

Flash is the code of the whole library (liblwext4.a): an upper bound,
since the linker leaves out what an application does not use.

The workloads run on a 3 MiB RAM disk, ext4 with a 1 MiB journal and
1 KiB blocks. The library has all its features, without debug output and
assertions (CONFIG_DEBUG_PRINTF=0, CONFIG_DEBUG_ASSERT=0), as a product
would ship it. Regenerate with `ci/run.sh bench update` (see
tests/bench/README.md).

| Operation | CPU | Block reads | Block writes | Peak heap | Peak stack |
|---|---|---|---|---|---|
| mkfs | 8,351,000 instructions | 11 | 216 | 8.7 KiB | 1.0 KiB |
| mount | 22,800 instructions | 5 | 3 | 3.4 KiB | 0.6 KiB |
| create | 65,280 instructions | 5 | 12 | 9.1 KiB | 0.8 KiB |
| write-4k | 123,880 instructions | 3 | 23 | 9.1 KiB | 0.9 KiB |
| write-1m | 15,932,560 instructions | 10 | 2828 | 9.1 KiB | 0.9 KiB |
| read-1m | 2,815,880 instructions | 260 | 0 | 8.7 KiB | 0.8 KiB |
| lookup-x100 | 3,416,400 instructions | 694 | 0 | 8.8 KiB | 0.9 KiB |
| truncate-1m | 120,840 instructions | 7 | 10 | 9.0 KiB | 0.9 KiB |
| unlink | 123,920 instructions | 19 | 14 | 9.2 KiB | 0.9 KiB |
| umount | 8,040 instructions | 0 | 3 | 7.6 KiB | 0.3 KiB |

Library (liblwext4.a, all of it): 100.3 KiB of code, 0.0 KiB of initialised data, 4.9 KiB of zeroed data.
