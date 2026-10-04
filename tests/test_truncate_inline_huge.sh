# SPDX-License-Identifier: BSD-3-Clause
# ext4 with inline_data (fork issue #162): /a and /b are inline files of
# 100 bytes (i_block and system.data) with a damaged size of about 2^64.
head -c 100 /dev/zero | tr '\000' 'z' >"$1.data"
lwext4_mke2fs -t ext4 -b 1024 -I 256 -O inline_data "$1" 8M
for f in a b; do
	debugfs -w -R "write $1.data $f" "$1" >/dev/null 2>&1
done
rm -f "$1.data"
debugfs -R 'stat /a' "$1" 2>/dev/null | grep -q 'Size of inline data: 100'
debugfs -w -f - "$1" >/dev/null 2>&1 <<'CMDS'
sif /a size 18446675177434513414
sif /b size 18446675177434513414
CMDS
