# Performance

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

Details per MCU: [cortex-m0](cortex-m0.md), [cortex-m3](cortex-m3.md), [cortex-m4](cortex-m4.md), [cortex-m7](cortex-m7.md).
