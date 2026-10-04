# SPDX-License-Identifier: BSD-3-Clause
# base: base
# The project site (ci/jobs/pages.sh): MkDocs with the Material theme turns
# the repository's Markdown and the pages docs/site/build.py generates into
# the site, Doxygen the headers into its API reference.
ARG BASE
FROM ${BASE}

RUN apt-get update && apt-get install -y --no-install-recommends \
	doxygen \
	mkdocs \
	mkdocs-material \
	&& rm -rf /var/lib/apt/lists/*
