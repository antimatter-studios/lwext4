# renode

Builds on [base](../base). Adds [Renode](https://renode.io) 1.17.0, Antmicro's
open source emulator for whole boards: the CPU core *and* the chip's
peripherals (UARTs, SPI controllers, GPIO, DMA, ...) and devices on its buses,
such as an SD card on SPI. Firmware runs unmodified, talking to the same
registers as on the real chip.

| Component | Version | Why |
|---|---|---|
| Renode portable Linux build | 1.17.0 (sha256 pinned, x86_64 and arm64 builds) | the emulator, `renode` / `renode-test` |
| libicu | Debian trixie | needed by the .NET runtime inside Renode |
| Robot Framework + renode-test deps | robotframework 6.1 and friends, in `/opt/renode-venv` | `renode-test` runs `.robot` test suites against the emulator |

The image works natively on x86_64 and arm64 (e.g. a Raspberry Pi 5), Renode
publishes portable builds for both.

Used by

- `ci/jobs/renode-baremetal-sdcard.sh <board>`: boots the image built by
  `ci/run.sh baremetal-sdcard-build <board>` (environment
  [arm-none-eabi](../arm-none-eabi)) and runs `tests/renode/lwext4.robot`
  against it. See [examples/baremetal-sdcard](../../../examples/baremetal-sdcard).

```sh
ci/run.sh baremetal-sdcard-build nucleo_f401re
ci/run.sh renode-baremetal-sdcard nucleo_f401re
ci/run.sh renode-baremetal-sdcard nucleo_f401re --include hostimg   # one test
ci/run.sh --shell renode                                            # explore
```

Inside the shell `renode --console --disable-gui` gives the interactive
monitor, e.g.

```
mach create
machine LoadPlatformDescription @examples/baremetal-sdcard/boards/nucleo_f401re/board.repl
machine SdCardFromFile @card.img sysbus.spi1 33554432 true "sdcard"
sysbus LoadHEX @examples/baremetal-sdcard/dist/nucleo_f401re/lwext4-example-nucleo_f401re.hex
emulation CreateServerSocketTerminal 3456 "term"
connector Connect sysbus.usart2 term
start
```

and `telnet localhost 3456` (from a second `ci/run.sh --shell renode`, or
publish the port) is the board's serial console.

What it teaches: testing firmware against models of the real peripherals,
and fault injection (the power cut test stops the emulation at an arbitrary
point of virtual time and boots a new machine with the same card).
