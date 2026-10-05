/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ST NUCLEO-L552ZE-Q: STM32L552ZE, Cortex-M33 (TrustZone disabled),
 * 512 KiB flash, 256 KiB SRAM.
 *
 * Console: LPUART1 on PG7/PG8 (ST-LINK virtual COM port). Port G is
 *          powered from VDDIO2, which has to be declared valid first.
 * SD card: SPI1 on the Arduino header, SCK D13/PA5, MISO D12/PA6,
 *          MOSI D11/PA7, CS D10/PD14.
 * Clock: reset default, 4 MHz MSI.
 */
#include "board.h"

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC 0x40021000u
#define RCC_AHB2ENR REG(RCC + 0x4c)
#define RCC_APB1ENR1 REG(RCC + 0x58)
#define RCC_APB1ENR2 REG(RCC + 0x5c)
#define RCC_APB2ENR REG(RCC + 0x60)
#define PWR_CR2 REG(0x40007004u)

#define GPIOA 0x42020000u
#define GPIOD 0x42020c00u
#define GPIOG 0x42021800u
#define GPIO_MODER(p) REG((p) + 0x00)
#define GPIO_OSPEEDR(p) REG((p) + 0x08)
#define GPIO_PUPDR(p) REG((p) + 0x0c)
#define GPIO_BSRR(p) REG((p) + 0x18)
#define GPIO_AFR(p, pin) REG((p) + 0x20 + 4 * ((pin) / 8))

#define LPUART1 0x40008000u
#define UART_CR1 REG(LPUART1 + 0x00)
#define UART_BRR REG(LPUART1 + 0x0c)
#define UART_ISR REG(LPUART1 + 0x1c)
#define UART_RDR REG(LPUART1 + 0x24)
#define UART_TDR REG(LPUART1 + 0x28)

#define SPI1 0x40013000u
#define SPI_CR1 REG(SPI1 + 0x00)
#define SPI_CR2 REG(SPI1 + 0x04)
#define SPI_SR REG(SPI1 + 0x08)
#define SPI_DR8 (*(volatile uint8_t *)(SPI1 + 0x0c))

#define CS_PIN 14u /* PD14 */

const char board_name[] = "nucleo_l552ze_q";
const char board_mcu[] = "STM32L552ZE Cortex-M33";

static void pin_af(uint32_t port, unsigned pin, unsigned af)
{
	GPIO_MODER(port) = (GPIO_MODER(port) & ~(3u << 2 * pin)) | 2u << 2 * pin;
	GPIO_OSPEEDR(port) |= 3u << 2 * pin;
	GPIO_AFR(port, pin) = (GPIO_AFR(port, pin) & ~(0xfu << 4 * (pin % 8))) |
			      af << 4 * (pin % 8);
}

void board_init(void)
{
	RCC_APB1ENR1 |= 1u << 28;                  /* PWR */
	PWR_CR2 |= 1u << 9;                        /* IOSV: VDDIO2 valid */
	RCC_AHB2ENR |= 1u << 0 | 1u << 3 | 1u << 6; /* GPIOA, GPIOD, GPIOG */
	RCC_APB1ENR2 |= 1u << 0;                   /* LPUART1 */
	RCC_APB2ENR |= 1u << 12;                   /* SPI1 */

	pin_af(GPIOG, 7, 8);
	pin_af(GPIOG, 8, 8);
	UART_BRR = (uint32_t)(256ull * 4000000u / 115200u);
	UART_CR1 = 1u << 3 | 1u << 2 | 1u << 0; /* TE, RE, UE */

	pin_af(GPIOA, 5, 5);
	pin_af(GPIOA, 6, 5);
	GPIO_PUPDR(GPIOA) |= 1u << 2 * 6; /* pull-up on MISO */
	pin_af(GPIOA, 7, 5);
	GPIO_BSRR(GPIOD) = 1u << CS_PIN;
	GPIO_MODER(GPIOD) = (GPIO_MODER(GPIOD) & ~(3u << 2 * CS_PIN)) |
			    1u << 2 * CS_PIN;
	/* 8 bit frames, RXNE as soon as one byte is in the RX FIFO */
	SPI_CR2 = 7u << 8 | 1u << 12;
	board_spi_fast(0);
}

void board_uart_putc(char c)
{
	while (!(UART_ISR & (1u << 7)))
		;
	UART_TDR = (uint8_t)c;
}

int board_uart_getc(void)
{
	while (!(UART_ISR & (1u << 5)))
		;
	return (int)(UART_RDR & 0xff);
}

void board_spi_cs(int asserted)
{
	GPIO_BSRR(GPIOD) = asserted ? 1u << (CS_PIN + 16) : 1u << CS_PIN;
}

void board_spi_fast(int fast)
{
	/* master, mode 0, software NSS; /16 = 250 kHz or /2 = 2 MHz */
	uint32_t br = fast ? 0u : 3u;
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
