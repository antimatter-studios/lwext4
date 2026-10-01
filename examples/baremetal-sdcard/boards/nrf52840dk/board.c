/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Nordic nRF52840 DK (PCA10056): nRF52840, Cortex-M4F, 1 MiB flash,
 * 256 KiB RAM.
 *
 * Console: UARTE0, TX P0.06 / RX P0.08 (J-Link virtual COM port).
 * SD card: SPIM2 with EasyDMA on the Arduino header, SCK D13/P1.15,
 *          MISO D12/P1.14, MOSI D11/P1.13, CS D10/P1.12.
 *
 * Both peripherals move data with EasyDMA, like the nrfx drivers: the CPU
 * only programs a RAM pointer and a length. EasyDMA can only reach RAM, so
 * every buffer handed to it must live in RAM (never const data in flash).
 */
#include "board.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define UARTE0 0x40002000u
#define SPIM2 0x40023000u
#define P1 0x50000300u

/* common EasyDMA peripheral register offsets */
#define TASKS_STARTRX 0x000
#define TASKS_START 0x010
#define TASKS_STARTTX 0x008
#define EVENTS_ENDRX 0x110
#define EVENTS_END 0x118
#define EVENTS_ENDTX 0x120
#define ENABLE 0x500
#define RXD_PTR 0x534
#define RXD_MAXCNT 0x538
#define TXD_PTR 0x544
#define TXD_MAXCNT 0x548

#define GPIO_OUTSET 0x508
#define GPIO_OUTCLR 0x50c
#define GPIO_DIRSET 0x518

#define PIN(port, n) ((port) << 5 | (n))
#define CS_PIN 12u /* P1.12 */

const char board_name[] = "nrf52840dk";
const char board_mcu[] = "nRF52840 Cortex-M4F";

void board_init(void)
{
	/* UARTE0 115200 8N1 */
	REG(UARTE0 + 0x50c) = PIN(0, 6);  /* PSEL.TXD */
	REG(UARTE0 + 0x514) = PIN(0, 8);  /* PSEL.RXD */
	REG(UARTE0 + 0x524) = 0x01d7e000; /* BAUDRATE 115200 */
	REG(UARTE0 + ENABLE) = 8;

	/* SD card chip select, idle high */
	REG(P1 + GPIO_OUTSET) = 1u << CS_PIN;
	REG(P1 + GPIO_DIRSET) = 1u << CS_PIN;

	/* SPIM2, mode 0, MSB first, 0xff clocked out when TX runs short */
	REG(SPIM2 + 0x508) = PIN(1, 15); /* PSEL.SCK */
	REG(SPIM2 + 0x50c) = PIN(1, 13); /* PSEL.MOSI */
	REG(SPIM2 + 0x510) = PIN(1, 14); /* PSEL.MISO */
	REG(SPIM2 + 0x554) = 0;          /* CONFIG */
	REG(SPIM2 + 0x5c0) = 0xff;       /* ORC */
	board_spi_fast(0);
	REG(SPIM2 + ENABLE) = 7;
}

void board_uart_putc(char c)
{
	static uint8_t tx; /* EasyDMA source must be in RAM */

	tx = (uint8_t)c;
	REG(UARTE0 + TXD_PTR) = (uint32_t)&tx;
	REG(UARTE0 + TXD_MAXCNT) = 1;
	REG(UARTE0 + EVENTS_ENDTX) = 0;
	REG(UARTE0 + TASKS_STARTTX) = 1;
	while (!REG(UARTE0 + EVENTS_ENDTX))
		;
}

int board_uart_getc(void)
{
	static uint8_t rx;

	REG(UARTE0 + RXD_PTR) = (uint32_t)&rx;
	REG(UARTE0 + RXD_MAXCNT) = 1;
	REG(UARTE0 + EVENTS_ENDRX) = 0;
	REG(UARTE0 + TASKS_STARTRX) = 1;
	while (!REG(UARTE0 + EVENTS_ENDRX))
		;
	return rx;
}

void board_spi_cs(int asserted)
{
	REG(P1 + (asserted ? GPIO_OUTCLR : GPIO_OUTSET)) = 1u << CS_PIN;
}

void board_spi_fast(int fast)
{
	/* FREQUENCY: K250 for identification, M8 afterwards */
	REG(SPIM2 + 0x524) = fast ? 0x80000000u : 0x04000000u;
}

static void spim_xfer(const uint8_t *tx, uint32_t ntx, uint8_t *rx,
		      uint32_t nrx)
{
	REG(SPIM2 + TXD_PTR) = (uint32_t)tx;
	REG(SPIM2 + TXD_MAXCNT) = ntx;
	REG(SPIM2 + RXD_PTR) = (uint32_t)rx;
	REG(SPIM2 + RXD_MAXCNT) = nrx;
	REG(SPIM2 + EVENTS_END) = 0;
	REG(SPIM2 + TASKS_START) = 1;
	while (!REG(SPIM2 + EVENTS_END))
		;
}

uint8_t board_spi_xfer(uint8_t out)
{
	static uint8_t tx, rx;

	tx = out;
	spim_xfer(&tx, 1, &rx, 1);
	return rx;
}

void board_spi_xfer_block(const uint8_t *tx, uint8_t *rx, size_t len)
{
	/* MAXCNT is 16 bits wide on the nRF52840 */
	while (len) {
		uint32_t n = len > 0xffff ? 0xffff : (uint32_t)len;
		spim_xfer(tx, tx ? n : 0, rx, rx ? n : 0);
		if (tx)
			tx += n;
		if (rx)
			rx += n;
		len -= n;
	}
}
