#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Run one example firmware in QEMU on its disk image, and check with
# e2fsprogs what it leaves there (CTest tests firmware-<app>, see
# CMakeLists.txt). Runs in the test's own directory.
#
#   check.sh <app> <firmware.elf> <qemu-system-arm> <machine>
set -eu
app=$1
elf=$2
qemu=$3
machine=$4
export PATH="$PATH:/sbin:/usr/sbin"

die()
{
	echo "FAIL: $*"
	exit 1
}

# die_log <log> <message>: the end of a run's output, then die
die_log()
{
	tail -n 20 "$1"
	shift
	die "$*"
}

# run <log> <semihosting args>...: the firmware's exit status is QEMU's;
# its output in <log>
run()
{
	log=$1
	shift
	cfg=enable=on,target=native
	for a in "$@"; do
		cfg="$cfg,arg=$a"
	done
	rc=0
	timeout 600 "$qemu" -M "$machine" -nographic -monitor none \
		-serial none -semihosting-config "$cfg" -kernel "$elf" \
		>"$log" 2>&1 || rc=$?
	return $rc
}

fsck_clean()
{
	e2fsck -fn disk.img >fsck.log 2>&1 || { cat fsck.log; die "e2fsck: $1"; }
}

case "$app" in
hello)
	rm -f disk.img
	truncate -s 8M disk.img
	run run.log disk.img || die_log run.log "hello exited with an error"
	cat run.log
	fsck_clean "after hello"
	text=$(debugfs -R "cat /hello.txt" disk.img 2>/dev/null)
	[ "$text" = "Hello from lwext4" ] || die "/hello.txt is '$text'"
	;;

datalogger)
	rm -f disk.img
	truncate -s 8M disk.img
	# First start: formats, then logs
	run run.log disk.img records=120 || die_log run.log "first run failed"
	grep -q "^datalogger: records 1..120 in files 1..3$" run.log ||
		die_log run.log "first run did not log records 1..120"
	cat run.log
	fsck_clean "after the first run"
	last=120
	rm -f cuts.log
	# Power cuts at different points, each followed by a normal start:
	# the records the cut run reported as appended are all there, at
	# most the one being written is lost, and the run goes on from it
	# (during mount and journal start, then at every block write of a
	# stretch of several records, and once late)
	for cut in 3 $(seq 100 160) 402; do
		if run cut.log disk.img "cut=$cut" records=400 trace; then
			die_log cut.log "cut=$cut: the run did not lose power"
		fi
		grep -q "^POWER CUT" cut.log || die_log cut.log "cut=$cut: no power cut"
		# The last record reported on the disk: an appended one, or
		# the last of the run before when the cut came first
		acked=$(sed -n 's/^appended //p' cut.log | tail -n 1)
		acked=${acked:-$last}
		run run.log disk.img records=30 ||
			die_log run.log "cut=$cut: the start after the cut failed"
		found=$(sed -n 's/^datalogger: found records [0-9]*\.\.\([0-9]*\) .*/\1/p' run.log)
		[ -n "$found" ] && [ "$found" -ge "$acked" ] &&
			[ "$found" -le $((acked + 1)) ] ||
			die_log run.log "cut=$cut: $acked records appended, ${found:-none} found after recovery"
		fsck_clean "after the power cut at write $cut"
		echo "cut=$cut: $acked appended before the cut, $found after recovery" >>cuts.log
		last=$(sed -n 's/^datalogger: records [0-9]*\.\.\([0-9]*\) .*/\1/p' run.log)
	done
	echo "$(wc -l <cuts.log) power cuts:"
	sed -n '1p;$p' cuts.log
	# The files, read by debugfs: whole records, numbered without a gap
	debugfs -R "ls -p /log" disk.img 2>/dev/null |
		awk -F/ '$6 ~ /^[0-9][0-9][0-9][0-9]\.csv$/ { print $6 }' | sort |
		while read -r f; do
			debugfs -R "cat /log/$f" disk.img 2>/dev/null
		done >records.txt
	awk -F, -v last="$last" '
		!/^[0-9][0-9][0-9][0-9][0-9][0-9],[0-9][0-9][0-9][0-9]$/ { print "bad line " NR ": " $0; exit 1 }
		$2 + 0 != ($1 * 7) % 10000 { print "bad value: " $0; exit 1 }
		NR > 1 && $1 + 0 != prev + 1 { print "gap after " prev; exit 1 }
		{ prev = $1 + 0 }
		END { if (prev != last) { print "last record " prev ", firmware said " last; exit 1 } }
	' records.txt || die "the log files read by debugfs"
	echo "datalogger: $(wc -l <records.txt) records in /log, the last $last"
	;;

reader)
	rm -rf tree disk.img
	mkdir -p tree/assets
	printf 'name=lwext4 reader\nversion=3\n# a comment\nbaud=115200\n' >tree/config.txt
	python3 -c 'import sys; sys.stdout.buffer.write(bytes((i * 31 + i // 7) & 255 for i in range(5000)))' >tree/assets/logo.bin
	printf 'the quick brown fox\n' >tree/assets/note.txt
	mke2fs -q -F -t ext4 -b 1024 -d tree disk.img 4M
	before=$(sha256sum disk.img)
	run run.log disk.img || die_log run.log "reader exited with an error"
	cat run.log
	[ "$(sha256sum disk.img)" = "$before" ] || die "the read-only mount changed the disk"
	grep '^config: ' run.log >got-config.txt
	printf 'config: name=lwext4 reader\nconfig: version=3\nconfig: baud=115200\n' >want-config.txt
	cmp want-config.txt got-config.txt || die "config lines differ"
	grep '^asset: ' run.log | sort >got-assets.txt
	python3 - tree/assets <<'EOF' | sort >want-assets.txt
import os, sys
for name in os.listdir(sys.argv[1]):
    data = open(os.path.join(sys.argv[1], name), "rb").read()
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    print("asset: %s %d %08x" % (name, len(data), h))
EOF
	cmp want-assets.txt got-assets.txt || die "asset sizes or checksums differ"
	;;

*)
	die "unknown app '$app'"
	;;
esac
echo "PASS: $app"
