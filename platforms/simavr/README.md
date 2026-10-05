ATmega1284 under simavr
=======================

The 8-bit AVR target of `tests/baremetal`: an ATmega1284 (128 KiB of
flash, 16 KiB of RAM) that [simavr](https://github.com/buserror/simavr)
runs at 16 MHz. [platform.c](platform.c) sends stdout to USART0, which
simavr prints, and stops the machine by sleeping with interrupts off,
which simavr takes as the end. simavr cannot pass an exit status on, so a
run's result is its PASS or FAIL line.

[simavr.cmake](simavr.cmake) has the runner. `ci/run.sh avr` builds and
runs `tests/baremetal` on it.
