/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ST NUCLEO-G071RB: STM32G071RB, Cortex-M0+, 128 KiB flash, 36 KiB SRAM.
 *
 * Console: USART2 on PA2/PA3 (ST-LINK virtual COM port).
 * SD card: SPI1 on the Arduino header, SCK D13/PA5, MISO D12/PA6,
 *          MOSI D11/PA7, CS D10/PB0.
 * Clock: reset default, 16 MHz HSI.
 */
#include "board.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC 0x40021000u
#define RCC_IOPENR REG(RCC + 0x34)
#define RCC_APBENR1 REG(RCC + 0x3c)
#define RCC_APBENR2 REG(RCC + 0x40)

#define GPIOA 0x50000000u
#define GPIOB 0x50000400u
#define GPIO_MODER(p) REG((p) + 0x00)
#define GPIO_OSPEEDR(p) REG((p) + 0x08)
#define GPIO_PUPDR(p) REG((p) + 0x0c)
#define GPIO_BSRR(p) REG((p) + 0x18)
#define GPIO_AFRL(p) REG((p) + 0x20)

#define USART2 0x40004400u
#define USART_CR1 REG(USART2 + 0x00)
#define USART_BRR REG(USART2 + 0x0c)
#define USART_ISR REG(USART2 + 0x1c)
#define USART_RDR REG(USART2 + 0x24)
#define USART_TDR REG(USART2 + 0x28)

#define SPI1 0x40013000u
#define SPI_CR1 REG(SPI1 + 0x00)
#define SPI_CR2 REG(SPI1 + 0x04)
#define SPI_SR REG(SPI1 + 0x08)
#define SPI_DR8 (*(volatile uint8_t *)(SPI1 + 0x0c))

#define CS_PIN 0u /* PB0 */

const char board_name[] = "nucleo_g071rb";
const char board_mcu[] = "STM32G071RB Cortex-M0+";

static void pin_af(uint32_t port, unsigned pin, unsigned af)
{
	GPIO_MODER(port) = (GPIO_MODER(port) & ~(3u << 2 * pin)) | 2u << 2 * pin;
	GPIO_OSPEEDR(port) |= 3u << 2 * pin;
	GPIO_AFRL(port) = (GPIO_AFRL(port) & ~(0xfu << 4 * pin)) | af << 4 * pin;
}

void board_init(void)
{
	RCC_IOPENR |= 1u << 0 | 1u << 1; /* GPIOA, GPIOB */
	RCC_APBENR1 |= 1u << 17;         /* USART2 */
	RCC_APBENR2 |= 1u << 12;         /* SPI1 */

	pin_af(GPIOA, 2, 1);
	pin_af(GPIOA, 3, 1);
	USART_BRR = 16000000u / 115200u;
	USART_CR1 = 1u << 3 | 1u << 2 | 1u << 0; /* TE, RE, UE */

	pin_af(GPIOA, 5, 0);
	pin_af(GPIOA, 6, 0);
	GPIO_PUPDR(GPIOA) |= 1u << 2 * 6; /* pull-up on MISO */
	pin_af(GPIOA, 7, 0);
	GPIO_BSRR(GPIOB) = 1u << CS_PIN;
	GPIO_MODER(GPIOB) = (GPIO_MODER(GPIOB) & ~(3u << 2 * CS_PIN)) |
			    1u << 2 * CS_PIN;
	/* 8 bit frames, RXNE as soon as one byte is in the RX FIFO */
	SPI_CR2 = 7u << 8 | 1u << 12;
	board_spi_fast(0);
}

void board_uart_putc(char c)
{
	while (!(USART_ISR & (1u << 7)))
		;
	USART_TDR = (uint8_t)c;
}

int board_uart_getc(void)
{
	while (!(USART_ISR & (1u << 5)))
		;
	return (int)(USART_RDR & 0xff);
}

void board_spi_cs(int asserted)
{
	GPIO_BSRR(GPIOB) = asserted ? 1u << (CS_PIN + 16) : 1u << CS_PIN;
}

void board_spi_fast(int fast)
{
	/* master, mode 0, software NSS; /64 = 250 kHz or /2 = 8 MHz */
	uint32_t br = fast ? 0u : 5u;
	SPI_CR1 &= ~(1u << 6);
	SPI_CR1 = 1u << 9 | 1u << 8 | br << 3 | 1u << 2;
	SPI_CR1 |= 1u << 6; /* SPE */
}

uint8_t board_spi_xfer(uint8_t out)
{
	/* 8 bit access: a 16 bit write would queue two frames */
	SPI_DR8 = out;
	while (!(SPI_SR & (1u << 0)))
		;
	return SPI_DR8;
}

void board_spi_xfer_block(const uint8_t *tx, uint8_t *rx, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t v = board_spi_xfer(tx ? tx[i] : 0xff);
		if (rx)
			rx[i] = v;
	}
}
