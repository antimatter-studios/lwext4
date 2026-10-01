/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Board support interface used by the Renode test firmware.
 *
 * Every board directory implements these functions with plain register
 * accesses for its MCU: clock gating, pin muxing, a polled console UART and
 * the SPI controller the SD card hangs off (the pins are the ones an SD card
 * breakout would use on the board's Arduino-style header).
 */
#ifndef BOARD_H_
#define BOARD_H_

#include <stddef.h>
#include <stdint.h>

extern const char board_name[];
extern const char board_mcu[];

void board_init(void);

/* Console */
void board_uart_putc(char c);
/* Blocking read of one character. */
int board_uart_getc(void);

/* SPI bus to the SD card */
void board_spi_cs(int asserted);
/* false: <= 400 kHz identification clock, true: full speed */
void board_spi_fast(int fast);
uint8_t board_spi_xfer(uint8_t out);
/*
 * Full duplex block transfer. tx == NULL clocks out 0xff, rx == NULL
 * discards the received bytes. Boards with a DMA capable SPI (nRF52 SPIM)
 * implement this with DMA, the others with a polled loop.
 */
void board_spi_xfer_block(const uint8_t *tx, uint8_t *rx, size_t len);

#endif /* BOARD_H_ */
