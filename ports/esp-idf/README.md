# lwext4 for ESP-IDF

`lwext4/` is an ESP-IDF component: add this directory to
`EXTRA_COMPONENT_DIRS`, list `lwext4` in your component's requirements, and
use the block devices in `lwext4/include/ext4_esp.h` (SD cards via
SDMMC/SDSPI, SPI flash data partitions). Options live under
`idf.py menuconfig` -> Component config -> lwext4.

A complete walkthrough - building, running in QEMU, flashing a board,
wiring an SD card - is in [examples/esp-idf](../../examples/esp-idf/README.md).
