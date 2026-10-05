/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * SD card over SPI, see sd_spi.h.
 */
#include "sd_spi.h"

#include <stdio.h>
#include <string.h>

#include "board.h"

struct sd_card sd;

#define CMD0 0    /* GO_IDLE_STATE */
#define CMD8 8    /* SEND_IF_COND */
#define CMD9 9    /* SEND_CSD */
#define CMD12 12  /* STOP_TRANSMISSION */
#define CMD13 13  /* SEND_STATUS */
#define CMD16 16  /* SET_BLOCKLEN */
#define CMD17 17  /* READ_SINGLE_BLOCK */
#define CMD18 18  /* READ_MULTIPLE_BLOCK */
#define CMD24 24  /* WRITE_BLOCK */
#define CMD25 25  /* WRITE_MULTIPLE_BLOCK */
#define CMD55 55  /* APP_CMD */
#define CMD58 58  /* READ_OCR */
#define CMD59 59  /* CRC_ON_OFF */
#define ACMD41 41 /* SD_SEND_OP_COND */

#define R1_IDLE 0x01u
#define R1_ILLEGAL 0x04u

#define TOKEN_START 0xfeu
#define TOKEN_START_MULTI 0xfcu
#define TOKEN_STOP_MULTI 0xfdu

/* Poll limits, in bytes clocked. Generous: a real card may take ~250 ms. */
#define WAIT_TOKEN 200000u
#define WAIT_BUSY 500000u
#define WAIT_INIT 20000u

static uint8_t crc7(const uint8_t *p, unsigned n)
{
	uint8_t crc = 0;
	while (n--) {
		uint8_t d = *p++;
		for (int i = 0; i < 8; i++) {
			crc <<= 1;
			if ((d ^ crc) & 0x80)
				crc ^= 0x09;
			d <<= 1;
		}
	}
	return (uint8_t)((crc << 1) | 1);
}

/* CRC-16/XMODEM (polynomial 0x1021) of the data blocks, table driven: the
 * bitwise version costs more CPU time than the SPI transfer itself. */
static const uint16_t crc16_table[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
};

static uint16_t crc16(const uint8_t *p, unsigned n)
{
	uint16_t crc = 0;
	while (n--)
		crc = (uint16_t)(crc << 8) ^ crc16_table[(crc >> 8) ^ *p++];
	return crc;
}

static int wait_ready(void)
{
	for (uint32_t i = 0; i < WAIT_BUSY; i++)
		if (board_spi_xfer(0xff) == 0xff)
			return 0;
	return -1;
}

static void deselect(void)
{
	board_spi_cs(0);
	board_spi_xfer(0xff); /* release DO */
}

static int select(void)
{
	board_spi_cs(1);
	board_spi_xfer(0xff);
	if (wait_ready() == 0)
		return 0;
	deselect();
	return -1;
}

/* Sends a command with CS already asserted, returns R1 (0xff: timeout). */
static uint8_t send_cmd_raw(uint8_t cmd, uint32_t arg)
{
	uint8_t frame[6];
	uint8_t r1;

	if (cmd == ACMD41) {
		r1 = send_cmd_raw(CMD55, 0);
		if (r1 > 1)
			return r1;
	}
	sd.cmds++;
	frame[0] = (uint8_t)(0x40 | cmd);
	frame[1] = (uint8_t)(arg >> 24);
	frame[2] = (uint8_t)(arg >> 16);
	frame[3] = (uint8_t)(arg >> 8);
	frame[4] = (uint8_t)arg;
	frame[5] = crc7(frame, 5);
	board_spi_xfer_block(frame, NULL, sizeof(frame));
	if (cmd == CMD12)
		board_spi_xfer(0xff); /* stuff byte */
	for (int i = 0; i < 10; i++) {
		r1 = board_spi_xfer(0xff);
		if (!(r1 & 0x80))
			return r1;
	}
	return 0xff;
}

static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
	uint8_t r1;
	deselect();
	if (select() != 0)
		return 0xff;
	r1 = send_cmd_raw(cmd, arg);
	return r1;
}

static int rx_datablock(uint8_t *buf, unsigned len)
{
	uint8_t token = 0xff;
	uint8_t crc[2];

	for (uint32_t i = 0; i < WAIT_TOKEN && token == 0xff; i++)
		token = board_spi_xfer(0xff);
	if (token != TOKEN_START) {
		printf("sd: bad read token 0x%02x\n", token);
		return -1;
	}
	board_spi_xfer_block(NULL, buf, len);
	board_spi_xfer_block(NULL, crc, 2);
	if (crc16(buf, len) != (uint16_t)(crc[0] << 8 | crc[1])) {
		sd.crc_errors++;
		printf("sd: data CRC mismatch\n");
		return -1;
	}
	return 0;
}

static int tx_datablock(const uint8_t *buf, uint8_t token)
{
	uint8_t trailer[2];
	uint8_t resp;
	uint16_t crc;

	if (wait_ready() != 0)
		return -1;
	board_spi_xfer(token);
	if (token == TOKEN_STOP_MULTI)
		return 0;
	crc = crc16(buf, SD_SECTOR_SIZE);
	trailer[0] = (uint8_t)(crc >> 8);
	trailer[1] = (uint8_t)crc;
	board_spi_xfer_block(buf, NULL, SD_SECTOR_SIZE);
	board_spi_xfer_block(trailer, NULL, 2);
	resp = board_spi_xfer(0xff);
	if ((resp & 0x1f) != 0x05) {
		printf("sd: data rejected 0x%02x\n", resp);
		return -1;
	}
	return 0;
}

static uint32_t csd_bits(unsigned msb, unsigned width)
{
	/* CSD is big endian, bit 127 first */
	uint32_t v = 0;
	for (unsigned b = msb + 1 - width; b <= msb; b++) {
		unsigned byte = 15 - b / 8;
		if (sd.csd[byte] & (1u << (b % 8)))
			v |= 1u << (b - (msb + 1 - width));
	}
	return v;
}

int sd_init(void)
{
	uint8_t r1, r7[4];
	uint32_t i;

	memset(&sd, 0, sizeof(sd));
	board_spi_fast(0);
	/* >= 74 clocks with CS and DI high */
	board_spi_cs(0);
	for (i = 0; i < 10; i++)
		board_spi_xfer(0xff);

	for (i = 0; i < 100; i++) {
		r1 = send_cmd(CMD0, 0);
		if (r1 == R1_IDLE)
			break;
	}
	if (r1 != R1_IDLE)
		return -1;

	r1 = send_cmd(CMD8, 0x1aa);
	if (r1 == R1_IDLE) {
		board_spi_xfer_block(NULL, r7, 4);
		if (r7[2] != 0x01 || r7[3] != 0xaa)
			return -2; /* voltage range not accepted */
		sd.v2 = true;
	} else if (!(r1 & R1_ILLEGAL)) {
		return -3;
	}

	/* Enable CRC checking before the card leaves the idle state. */
	if (send_cmd(CMD59, 1) > 1)
		return -4;

	for (i = 0; i < WAIT_INIT; i++) {
		r1 = send_cmd(ACMD41, sd.v2 ? (1u << 30) : 0);
		if (r1 == 0)
			break;
	}
	if (r1 != 0)
		return -5;

	if (sd.v2) {
		if (send_cmd(CMD58, 0) != 0)
			return -6;
		board_spi_xfer_block(NULL, sd.ocr, 4);
		sd.block_addr = (sd.ocr[0] & 0x40) != 0; /* CCS */
	}
	if (!sd.block_addr && send_cmd(CMD16, SD_SECTOR_SIZE) != 0)
		return -7;

	if (send_cmd(CMD9, 0) != 0 || rx_datablock(sd.csd, 16) != 0)
		return -8;
	if ((sd.csd[0] >> 6) == 1) { /* CSD 2.0 */
		sd.sectors = (csd_bits(69, 22) + 1) * 1024u;
	} else {                     /* CSD 1.0 */
		uint32_t c_size = csd_bits(73, 12);
		uint32_t mult = csd_bits(49, 3);
		uint32_t bl_len = csd_bits(83, 4);
		uint64_t bytes = (uint64_t)(c_size + 1) << (mult + 2 + bl_len);
		sd.sectors = (uint32_t)(bytes / SD_SECTOR_SIZE);
	}
	deselect();
	board_spi_fast(1);
	return 0;
}

static uint32_t sd_addr(uint32_t sector)
{
	return sd.block_addr ? sector : sector * SD_SECTOR_SIZE;
}

int sd_read(uint8_t *buf, uint32_t sector, uint32_t cnt)
{
	int ret = 0;

	if (cnt == 1) {
		if (send_cmd(CMD17, sd_addr(sector)) != 0 ||
		    rx_datablock(buf, SD_SECTOR_SIZE) != 0)
			ret = -1;
	} else {
		if (send_cmd(CMD18, sd_addr(sector)) != 0) {
			ret = -1;
		} else {
			for (uint32_t i = 0; i < cnt; i++) {
				if (rx_datablock(buf + i * SD_SECTOR_SIZE,
						 SD_SECTOR_SIZE) != 0) {
					ret = -1;
					break;
				}
			}
			if (send_cmd_raw(CMD12, 0) != 0 || wait_ready() != 0)
				ret = -1;
		}
	}
	deselect();
	if (ret == 0)
		sd.rd_blocks += cnt;
	else
		printf("sd: read of %lu sectors at %lu failed\n",
		       (unsigned long)cnt, (unsigned long)sector);
	return ret;
}

int sd_write(const uint8_t *buf, uint32_t sector, uint32_t cnt)
{
	int ret = 0;

	if (cnt == 1) {
		if (send_cmd(CMD24, sd_addr(sector)) != 0 ||
		    tx_datablock(buf, TOKEN_START) != 0)
			ret = -1;
	} else {
		if (send_cmd(CMD25, sd_addr(sector)) != 0) {
			ret = -1;
		} else {
			for (uint32_t i = 0; i < cnt; i++) {
				if (tx_datablock(buf + i * SD_SECTOR_SIZE,
						 TOKEN_START_MULTI) != 0) {
					ret = -1;
					break;
				}
			}
			if (tx_datablock(NULL, TOKEN_STOP_MULTI) != 0)
				ret = -1;
		}
	}
	/* Wait for the end of programming before releasing the card. */
	if (wait_ready() != 0)
		ret = -1;
	deselect();
	if (ret == 0)
		sd.wr_blocks += cnt;
	else
		printf("sd: write of %lu sectors at %lu failed\n",
		       (unsigned long)cnt, (unsigned long)sector);
	return ret;
}
