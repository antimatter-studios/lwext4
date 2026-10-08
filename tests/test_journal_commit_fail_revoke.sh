# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks and a journal whose blocks from JHOLE on are a hole
# (a damaged journal inode): a transaction that reaches them cannot be
# committed.
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
debugfs -w -R "punch <8> ${JHOLE:-26}" "$1" >/dev/null 2>&1
debugfs -R 'stat <8>' "$1" 2>/dev/null | grep -q "(0-$((${JHOLE:-26} - 1)))\|-$((${JHOLE:-26} - 1))):" ||
	{ echo "journal: no hole at block ${JHOLE:-26}" >&2; exit 1; }
