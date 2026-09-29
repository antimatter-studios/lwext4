/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ST NUCLEO-F401RE: STM32F401RE, Cortex-M4F, 512 KiB flash, 96 KiB SRAM.
 *
 * Console: USART2 on PA2/PA3 (ST-LINK virtual COM port).
 * SD card: SPI1 on the Arduino header, SCK D13/PA5, MISO D12/PA6,
 *          MOSI D11/PA7, CS D10/PB6.
 * Clock: reset default, 16 MHz HSI.
 */
#include "board.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC 0x40023800u
#define RCC_AHB1ENR REG(RCC + 0x30)
#define RCC_APB1ENR REG(RCC + 0x40)
#define RCC_APB2ENR REG(RCC + 0x44)

#define GPIOA 0x40020000u
#define GPIOB 0x40020400u
#define GPIO_MODER(p) REG((p) + 0x00)
#define GPIO_OSPEEDR(p) REG((p) + 0x08)
#define GPIO_PUPDR(p) REG((p) + 0x0c)
#define GPIO_BSRR(p) REG((p) + 0x18)
#define GPIO_AFRL(p) REG((p) + 0x20)

#define USART2 0x40004400u
#define USART_SR REG(USART2 + 0x00)
#define USART_DR REG(USART2 + 0x04)
#define USART_BRR REG(USART2 + 0x08)
#define USART_CR1 REG(USART2 + 0x0c)

#define SPI1 0x40013000u
#define SPI_CR1 REG(SPI1 + 0x00)
#define SPI_SR REG(SPI1 + 0x08)
#define SPI_DR REG(SPI1 + 0x0c)

#define CS_PIN 6u /* PB6 */

const char board_name[] = "nucleo_f401re";
const char board_mcu[] = "STM32F401RE Cortex-M4F";

static void pin_af(uint32_t port, unsigned pin, unsigned af)
{
	GPIO_MODER(port) = (GPIO_MODER(port) & ~(3u << 2 * pin)) | 2u << 2 * pin;
	GPIO_OSPEEDR(port) |= 3u << 2 * pin;
	GPIO_AFRL(port) = (GPIO_AFRL(port) & ~(0xfu << 4 * pin)) | af << 4 * pin;
}

void board_init(void)
{
	RCC_AHB1ENR |= 1u << 0 | 1u << 1; /* GPIOA, GPIOB */
	RCC_APB1ENR |= 1u << 17;          /* USART2 */
	RCC_APB2ENR |= 1u << 12;          /* SPI1 */

	pin_af(GPIOA, 2, 7);
	pin_af(GPIOA, 3, 7);
	USART_BRR = 16000000u / 115200u;
	USART_CR1 = 1u << 13 | 1u << 3 | 1u << 2; /* UE, TE, RE */

	pin_af(GPIOA, 5, 5);
	pin_af(GPIOA, 6, 5);
	GPIO_PUPDR(GPIOA) |= 1u << 2 * 6; /* pull-up on MISO */
	pin_af(GPIOA, 7, 5);
	GPIO_BSRR(GPIOB) = 1u << CS_PIN;
	GPIO_MODER(GPIOB) = (GPIO_MODER(GPIOB) & ~(3u << 2 * CS_PIN)) |
			    1u << 2 * CS_PIN;
	board_spi_fast(0);
}

void board_uart_putc(char c)
{
	while (!(USART_SR & (1u << 7)))
		;
	USART_DR = (uint8_t)c;
}

int board_uart_getc(void)
{
	while (!(USART_SR & (1u << 5)))
		;
	return (int)(USART_DR & 0xff);
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
	/* TXE is always set here: the previous byte has been received. */
	*(volatile uint8_t *)&SPI_DR = out;
	while (!(SPI_SR & (1u << 0)))
		;
	return (uint8_t)SPI_DR;
}

void board_spi_xfer_block(const uint8_t *tx, uint8_t *rx, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t v = board_spi_xfer(tx ? tx[i] : 0xff);
		if (rx)
			rx[i] = v;
	}
}
