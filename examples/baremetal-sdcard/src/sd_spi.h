/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * SD card driver for the SPI bus mode (SD Physical Layer Simplified
 * Specification, chapter 7), in the style of the drivers hobby projects
 * put in front of FatFs/lwext4: CMD0/CMD8/ACMD41/CMD58 identification,
 * CRC protected commands and data (CMD59), CMD17/CMD18 reads and
 * CMD24/CMD25 writes.
 */
#ifndef SD_SPI_H_
#define SD_SPI_H_

#include <stdbool.h>
#include <stdint.h>

#define SD_SECTOR_SIZE 512u

struct sd_card {
	bool v2;            /* answered CMD8 */
	bool block_addr;    /* SDHC/SDXC: sector addressed (OCR.CCS) */
	uint32_t sectors;   /* capacity from the CSD */
	uint8_t ocr[4];
	uint8_t csd[16];
	/* statistics */
	uint32_t cmds;
	uint32_t rd_blocks, wr_blocks;
	uint32_t crc_errors;
};

extern struct sd_card sd;

/* Returns 0 on success, a negative step number on failure. */
int sd_init(void);
/* 0 on success, -1 on error. buf must hold cnt * 512 bytes. */
int sd_read(uint8_t *buf, uint32_t sector, uint32_t cnt);
int sd_write(const uint8_t *buf, uint32_t sector, uint32_t cnt);

#endif /* SD_SPI_H_ */
