Simulators with a C library
===========================

Targets whose simulator gives the C library a console and `exit()`, so
the board code is just [platform.c](platform.c): `platform_init()` makes
stdout unbuffered, `platform_exit()` flushes it and calls `exit()`.

| Toolchain | CPU | C library | Runs in |
|---|---|---|---|
| `arm-sim` | ARM7TDMI (ARMv4T) | newlib with rdimon semihosting | `qemu-arm` (user mode) |
| `msp430-sim` | MSP430X, large memory model | libgloss for the simulator (`-msim`) | `msp430-elf-run`, the GDB simulator |

[hosted.cmake](hosted.cmake) has the runner of each. `tests/baremetal`
runs on both (`ci/run.sh arm-sim`, `ci/run.sh msp430`).
