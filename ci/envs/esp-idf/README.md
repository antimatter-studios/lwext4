# esp-idf environment

Everything needed to build lwext4 into ESP32-family firmware, flash it to a
board and run it in Espressif's QEMU — with nothing installed on the host
except docker.

| What | Version | Where from |
|------|---------|------------|
| ESP-IDF (framework, bootloader, drivers, FreeRTOS) | v5.5.5 | `espressif/idf:v5.5.5` (pinned by digest) |
| Xtensa GCC (esp32, esp32s3), RISC-V GCC (esp32c3) | as pinned by IDF v5.5.5 | same image |
| esptool, idf.py, CMake, Ninja, ccache | as pinned by IDF v5.5.5 | same image |
| Espressif QEMU (`qemu-system-xtensa`, `qemu-system-riscv32`) | as pinned by IDF v5.5.5 (`idf_tools.py`) | same image |
| e2fsprogs (mke2fs, e2fsck, debugfs) | Ubuntu 24.04 package | added here |

The image is multi-arch: it runs natively on x86_64 PCs/CI runners and on
arm64 machines such as a Raspberry Pi 4/5 or Apple Silicon (via Docker
Desktop).

## Why not `FROM` the shared `base` environment?

`espressif/idf` *is* Espressif's reference installation: it is what their
own documentation tells users to use, and it already contains the IDF
checkout, the toolchains and the tools-manager-installed QEMU. Recreating
it on top of `ci/envs/base` would give a less trustworthy copy of the same
thing, so this environment starts from the official image and adds only
e2fsprogs. QEMU is not split into a separate environment because the
official image already ships it; a build that is later flashed to real
hardware uses exactly the same image.

## Using it

From the repository root (the repository is mounted at `/src`):

```sh
ci/run.sh esp32-build esp32          # build examples/esp-idf for a chip
ci/run.sh esp32-qemu esp32c3         # build + run the firmware in QEMU
ci/run.sh --shell esp-idf            # interactive shell with idf.py on PATH
```

Inside the shell every ESP-IDF command works as documented by Espressif:

```sh
cd examples/esp-idf
idf.py set-target esp32s3
idf.py menuconfig                    # Component config -> lwext4
idf.py build
idf.py qemu monitor                  # run in QEMU, attach the monitor
```

To flash a board from the container, pass the serial device through. The
runner does not do that for you (it would need `--device`), so run docker
directly with the image `ci/run.sh` built (`docker images lwext4-ci-esp-idf`):

```sh
docker run --rm -it --device /dev/ttyUSB0 -v "$PWD":/src -w /src/examples/esp-idf \
    -u "$(id -u):$(id -g)" --group-add "$(stat -c %g /dev/ttyUSB0)" -e HOME=/tmp \
    lwext4-ci-esp-idf:<tag> idf.py -B build-esp32 -p /dev/ttyUSB0 flash monitor
```

or flash the images from the build artifacts with any esptool, see
`examples/esp-idf/README.md`.
