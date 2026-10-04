# SPDX-License-Identifier: BSD-3-Clause
# Directory trees with cycles (fork issue #150), made with debugfs ln:
#   /d/sub/back -> /d          (two steps)
#   /e/s1/s2/back -> /e        (three steps)
# and a normal /ok next to them.
lwext4_mke2fs -t ext4 -b 1024 -N 256 "$1" 8M
echo f >"$1.f"
debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
mkdir /d
mkdir /d/sub
write $1.f /d/sub/f
ln /d /d/sub/back
mkdir /e
mkdir /e/s1
mkdir /e/s1/s2
write $1.f /e/s1/s2/f
ln /e /e/s1/s2/back
mkdir /ok
write $1.f /ok/f
CMDS
rm -f "$1.f"
debugfs -R 'ls /d/sub' "$1" 2>/dev/null | grep -q back
debugfs -R 'ls /e/s1/s2' "$1" 2>/dev/null | grep -q back
