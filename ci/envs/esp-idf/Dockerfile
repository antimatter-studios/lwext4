# SPDX-License-Identifier: BSD-3-Clause
# ESP-IDF build + emulation environment for the ESP32 family.
#
# Starts from Espressif's official image instead of ci/envs/base: that image
# *is* the reference ESP-IDF installation (IDF checkout, xtensa/riscv32 GCC,
# esptool, CMake/Ninja, Python venv, and Espressif's QEMU fork for esp32,
# esp32c3 and esp32s3), published for linux/amd64 and linux/arm64. Rebuilding
# it from debian would only produce a less trustworthy copy of it.
# Pinned by tag and by digest of the multi-arch index.
FROM espressif/idf:v5.5.5@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2

# The only addition: e2fsprogs, the independent oracle. mke2fs builds the
# ext4 images flashed next to the firmware, e2fsck/debugfs check on the host
# what the firmware wrote.
RUN apt-get update && apt-get install -y --no-install-recommends \
	e2fsprogs \
	&& rm -rf /var/lib/apt/lists/*

# ci/run.sh runs jobs as the calling user (not root) with HOME=/tmp. The
# image's entrypoint sources $IDF_PATH/export.sh, so idf.py, the compilers
# and qemu-system-{xtensa,riscv32} are on PATH for every command. The IDF
# checkout belongs to root; let git (used by idf.py for version strings)
# read it and the mounted repository as any user.
RUN git config --system --add safe.directory '*'
