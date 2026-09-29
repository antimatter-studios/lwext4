# env: renode
# SPDX-License-Identifier: BSD-3-Clause
# Boot the flashable image built by baremetal-sdcard-build on the emulated
# board, with an SD card image on its SPI bus, and run
# tests/renode/lwext4.robot: card formatted on the host, ext4_mkfs on the
# device, power cuts during writes. e2fsck/debugfs check every card image
# afterwards.
#
#   ci/run.sh baremetal-sdcard-build <board>
#   ci/run.sh renode-baremetal-sdcard <board> [renode-test options]
#
# e.g. `--include hostimg` runs only the test tagged hostimg. Results and
# logs: examples/baremetal-sdcard/build-<board>/renode-results/
board=${1:?usage: renode-baremetal-sdcard.sh <board> [renode-test options]}
shift

proj=examples/baremetal-sdcard
build=$proj/build-$board
image=$proj/dist/$board/lwext4-example-$board.hex
if [ ! -f "$build/board.env" ] || [ ! -f "$image" ]; then
	echo "no firmware for $board, run: ci/run.sh baremetal-sdcard-build $board" >&2
	exit 1
fi
renode --version | head -n 1
rm -rf "$build/renode-results"
# boot the distributed image, not an intermediate build product
FIRMWARE=$(pwd)/$image tests/renode/run.sh "$build" "$@"
