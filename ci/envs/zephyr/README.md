# zephyr

Builds on [base](../base). Adds what it takes to build a Zephyr RTOS
application for Arm Cortex-M and run it in QEMU, pinned to exact versions:

| Component | Version | Why |
|---|---|---|
| Zephyr RTOS | v4.4.2 (shallow clone of the tag) | kernel, drivers (SPI, GPIO, SD host over SPI), the SD card stack, disk access API, build system |
| west workspace | Zephyr's `west.yml` with a project filter: only `cmsis_6` (depth 1, at the manifest's revision) | the Cortex-M core headers; the other ~90 modules (vendor HALs, crypto, networking, ...) are not needed |
| Zephyr SDK | 1.0.1 (the version Zephyr v4.4.2 asks for): the minimal bundle plus only the `arm-zephyr-eabi` GNU toolchain (GCC 14, picolibc), sha256 pinned, x86_64 and aarch64 host builds | the compiler Zephyr is tested with |
| west, pyelftools, PyYAML, pykwalify, jsonschema, packaging, anytree | pinned, in `/opt/zephyr-venv` | the Python side of the Zephyr build (devicetree, Kconfig, module and ELF processing) |
| ninja-build, device-tree-compiler | Debian trixie | build tool; `dtc` for devicetree warnings |
| qemu-system-arm | Debian trixie (QEMU 10.0) | runs the `qemu_cortex_m3` board: QEMU's `lm3s6965evb` machine, the TI Stellaris LM3S6965 evaluation board with an SD card in its microSD socket |

The image works natively on x86_64 and arm64 (e.g. a Raspberry Pi 5): the
Zephyr SDK publishes toolchains for both Linux hosts, and QEMU comes from
Debian. To keep it small, the SDK's other toolchains and host tools are not
installed, and the Zephyr tree loses what no build reads (its tests,
samples, documentation and board photos, about 600 MB). The environment
variables `ZEPHYR_BASE`, `ZEPHYR_SDK_INSTALL_DIR` and
`ZEPHYR_TOOLCHAIN_VARIANT=zephyr` point the build at them, so `west build`
works on an application anywhere, e.g. in the mounted repository.

Other boards: another Cortex-M board works the same way if its vendor HAL
is added to the project filter (e.g. `+hal_stm32`); another architecture
needs its SDK toolchain (`toolchain_gnu_<host>_<target>.tar.xz` from the same
release).

Used by

- `ci/jobs/zephyr-build.sh <board>`: builds [examples/zephyr](../../../examples/zephyr)
  with `west build` and collects the images in `examples/zephyr/dist/<board>/`.
- `ci/jobs/zephyr-qemu.sh <board>`: the same, then boots the image in QEMU
  with a blank SD card image, which the firmware formats and fills, and
  checks the card image with `e2fsck -fn` and `debugfs`.

```sh
ci/run.sh zephyr-build qemu_cortex_m3
ci/run.sh zephyr-qemu qemu_cortex_m3
ci/run.sh --shell zephyr                  # explore
```

Inside the shell every west command works as documented by Zephyr, e.g.

```sh
west build -b qemu_cortex_m3 -d build-m3 examples/zephyr
west build -d build-m3 -t menuconfig      # Modules -> lwext4
truncate -s 32M build-m3/card.img         # a blank SD card
west build -d build-m3 -t run             # QEMU; Ctrl-a x quits
```

What it teaches: using lwext4 as a Zephyr module, with the storage side
(SPI controller, SD card protocol, disk access) left to Zephyr's own
drivers and described in devicetree.
