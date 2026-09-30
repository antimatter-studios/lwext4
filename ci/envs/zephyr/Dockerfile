# SPDX-License-Identifier: BSD-3-Clause
# base: base
# Zephyr RTOS for Cortex-M: a west workspace with Zephyr and only the modules
# a Cortex-M application needs (shallow clones, pinned), the Zephyr SDK's
# arm-zephyr-eabi GCC (the SDK bundle without its other toolchains), and
# qemu-system-arm to run the mps2/an385 board (Arm MPS2, Cortex-M3).
ARG BASE
FROM ${BASE}

ARG ZEPHYR_VERSION=v4.4.2
# The SDK version Zephyr v4.4.2 asks for (its SDK_VERSION file)
ARG ZEPHYR_SDK_VERSION=1.0.1
ARG SDK_SHA256_AMD64=ca9bc0ff66fafca1dac9d592a36d953cf16d096a9d09b1c0357f021cf9f6a7eb
ARG SDK_SHA256_ARM64=d79c5bfc68e679488659bea289a4026e52a64f03338875c8c9c850fff13cee30
ARG ARM_EABI_SHA256_AMD64=21b85981cb5a1818d9bc53d82af80f208946ec038b982ff1907287572ed3a634
ARG ARM_EABI_SHA256_ARM64=b9805b691f2f0a8926c92694cae378d05ba07b76abca745e216fcc52753cc4d6

RUN apt-get update && apt-get install -y --no-install-recommends \
	curl \
	device-tree-compiler \
	ninja-build \
	python3-venv \
	qemu-system-arm \
	&& rm -rf /var/lib/apt/lists/*

# Zephyr SDK: the "minimal" bundle (CMake package, no toolchains) plus the one
# GNU toolchain for Arm Cortex-M, for x86_64 or aarch64 hosts. Both archives
# are checked against the sha256 sums published with the release. This is
# what the SDK's setup.sh -t arm-zephyr-eabi does, minus the unchecked
# download. Transient server errors (HTTP 5xx) are retried.
RUN set -e; \
	case "$(dpkg --print-architecture)" in \
	amd64) host=linux-x86_64; sdk_sum=$SDK_SHA256_AMD64; tc_sum=$ARM_EABI_SHA256_AMD64 ;; \
	arm64) host=linux-aarch64; sdk_sum=$SDK_SHA256_ARM64; tc_sum=$ARM_EABI_SHA256_ARM64 ;; \
	*) echo "no Zephyr SDK for $(dpkg --print-architecture)"; exit 1 ;; \
	esac; \
	url=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$ZEPHYR_SDK_VERSION; \
	curl -fsSL --retry 5 -o /tmp/sdk.tar.xz "$url/zephyr-sdk-${ZEPHYR_SDK_VERSION}_${host}_minimal.tar.xz"; \
	echo "$sdk_sum  /tmp/sdk.tar.xz" | sha256sum -c -; \
	tar -xJf /tmp/sdk.tar.xz -C /opt; \
	curl -fsSL --retry 5 -o /tmp/tc.tar.xz "$url/toolchain_gnu_${host}_arm-zephyr-eabi.tar.xz"; \
	echo "$tc_sum  /tmp/tc.tar.xz" | sha256sum -c -; \
	mkdir -p /opt/zephyr-sdk-$ZEPHYR_SDK_VERSION/gnu; \
	tar -xJf /tmp/tc.tar.xz -C /opt/zephyr-sdk-$ZEPHYR_SDK_VERSION/gnu; \
	rm -f /tmp/sdk.tar.xz /tmp/tc.tar.xz; \
	rm -rf /opt/zephyr-sdk-$ZEPHYR_SDK_VERSION/hosttools

# Python tools of the Zephyr build (west, devicetree and ELF processing,
# module.yml validation), pinned
RUN python3 -m venv /opt/zephyr-venv && \
	/opt/zephyr-venv/bin/pip install --no-cache-dir \
		west==1.5.0 \
		pyelftools==0.33 \
		PyYAML==6.0.3 \
		pykwalify==1.8.0 \
		jsonschema==4.26.0 \
		packaging==26.3 \
		anytree==2.13.0

ENV PATH=/opt/zephyr-venv/bin:$PATH

# The west workspace: Zephyr at the release tag, and of the ~90 modules its
# manifest lists only CMSIS 6 (Cortex-M core headers), each a depth 1 clone
# of the revision the manifest pins. Parts of the Zephyr tree no build
# reads are deleted to keep the image small: its own tests and samples, the
# documentation and the board photos (about 600 MB).
RUN git clone --depth 1 --branch "$ZEPHYR_VERSION" \
		https://github.com/zephyrproject-rtos/zephyr.git /opt/zephyrproject/zephyr && \
	cd /opt/zephyrproject && \
	west init -l zephyr && \
	west config manifest.project-filter -- '-.*,+cmsis_6' && \
	west update --narrow --fetch-opt=--depth=1 && \
	west config zephyr.base zephyr && \
	west list && \
	rm -rf zephyr/tests zephyr/samples zephyr/doc && \
	find zephyr/boards -type d -name doc -prune -exec rm -rf {} +

# ci/run.sh runs jobs as the calling user with HOME=/tmp. Let git (Zephyr's
# build asks it for version strings) read the root owned workspace and the
# mounted repository.
RUN git config --system --add safe.directory '*'

ENV ZEPHYR_BASE=/opt/zephyrproject/zephyr \
	ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION} \
	ZEPHYR_TOOLCHAIN_VARIANT=zephyr
