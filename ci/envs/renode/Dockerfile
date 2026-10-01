# SPDX-License-Identifier: BSD-3-Clause
# base: base
# Renode: emulates whole microcontroller boards (CPU core, memory map and
# peripheral models such as SPI controllers, UARTs and an SD card), and its
# Robot Framework based test runner renode-test.
ARG BASE
FROM ${BASE}

ARG RENODE_VERSION=1.17.0
ARG RENODE_SHA256_AMD64=4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f
ARG RENODE_SHA256_ARM64=e9051578411441bfb47b77de0381cfba3badce03663367fab561472fd7807f1f

RUN apt-get update && apt-get install -y --no-install-recommends \
	curl \
	libicu76 \
	python3-venv \
	&& rm -rf /var/lib/apt/lists/*

# Renode's self-contained ("portable") Linux build, x86_64 or arm64
RUN set -e; \
	case "$(dpkg --print-architecture)" in \
	amd64) pkg=linux-portable; sum=$RENODE_SHA256_AMD64 ;; \
	arm64) pkg=linux-arm64-portable; sum=$RENODE_SHA256_ARM64 ;; \
	*) echo "no Renode build for $(dpkg --print-architecture)"; exit 1 ;; \
	esac; \
	curl -fsSL -o /tmp/renode.tar.gz \
		"https://github.com/renode/renode/releases/download/v$RENODE_VERSION/renode-$RENODE_VERSION.$pkg.tar.gz"; \
	echo "$sum  /tmp/renode.tar.gz" | sha256sum -c -; \
	mkdir -p /opt/renode; \
	tar -xzf /tmp/renode.tar.gz -C /opt/renode --strip-components=1; \
	rm /tmp/renode.tar.gz

# renode-test's Python dependencies (versions from its tests/requirements.txt)
RUN python3 -m venv /opt/renode-venv && \
	/opt/renode-venv/bin/pip install --no-cache-dir \
		robotframework==6.1 \
		robotframework-retryfailed==0.2.0 \
		psutil==7.1.0 \
		pyyaml==6.0.2 \
		telnetlib3==2.0.4

ENV PATH=/opt/renode:/opt/renode-venv/bin:$PATH
