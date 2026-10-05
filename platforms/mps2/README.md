MPS2 boards under QEMU
======================

Arm's MPS2 FPGA boards, as QEMU emulates them: `mps2-an385` (Cortex-M3;
Cortex-M0/M0+ code runs on it too), `mps2-an386` (Cortex-M4) and
`mps2-an500` (Cortex-M7). 4 MiB of RAM, no storage: the console and the
exit status go through ARM semihosting to the host.

| File | What |
|---|---|
| [startup.c](startup.c) | vector table, `.data`/`.bss` set-up, FPU enable, a fault handler that prints the faulting address, `platform_init()`, `platform_exit()`, `platform_cmdline()` |
| [mps2.ld](mps2.ld) | memory map: 4 MiB code at 0, 4 MiB RAM at 0x20000000 for data, heap and stack |
| [disk.c](disk.c) | `platform_disk()`: a disk image file on the host, through semihosting, unbuffered; `cut=<n>` on the command line cuts the power at a block write. On a real board, your storage driver goes here |
| [counter.c](counter.c) | `platform_counter()`: instructions, exact under QEMU's `-icount shift=0` (the benchmark's counter) |
| [mps2.cmake](mps2.cmake) | `platform_firmware(<target> <sources>)` and `${PLATFORM_RUNNER}` for CMake (through [platform.cmake](../platform.cmake)) |

Firmware is built with a Cortex-M toolchain file and run in QEMU:

```sh
cmake -S . -B build-m4 -DCMAKE_TOOLCHAIN_FILE=toolchain/cortex-m4.cmake
cmake --build build-m4
ctest --test-dir build-m4 -V     # tests/baremetal, tests/bench, examples/firmware in QEMU
```

or `ci/run.sh cortex-m cortex-m4` in CI's container.
