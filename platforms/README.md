Platforms
=========

The code that runs lwext4 on a board: start-up, linker script, console,
exit, counter. [platform.h](platform.h) is the API each one implements;
`tests/baremetal`, `tests/bench` and the [example firmware](../examples/firmware/README.md)
are written against it, so the board code an application starts from is the code CI
runs on every pull request.

| Platform | Boards | Runs in |
|---|---|---|
| [mps2](mps2/README.md) | Arm MPS2: AN385 (Cortex-M3, also M0/M0+ code), AN386 (M4), AN500 (M7) | QEMU |
| [hosted](hosted/README.md) | ARM7TDMI (`arm-sim`), MSP430X (`msp430-sim`) | qemu-arm, the MSP430 GDB simulator |
| [simavr](simavr/README.md) | ATmega1284 | simavr |

The boards of `examples/baremetal-sdcard` still have their own code; they
move here next (fork issue #169).

[platform.cmake](platform.cmake) picks the platform of the toolchain; CMake
code that builds firmware includes it and calls
`platform_firmware(<target> <sources>)`, and runs the result with
`${PLATFORM_RUNNER}`.

| API ([platform.h](platform.h)) | |
|---|---|
| `platform_init()`, `platform_exit(status)` | console up; stop with a status |
| `platform_disk()` | the storage, as an `ext4_blockdev` (mps2) |
| `platform_cmdline()` | how the machine was started, options for tests (mps2) |
| `platform_counter()` | a counter to measure with (mps2) |
