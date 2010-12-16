/* LPC1758 SD driver
 *
 * Jacob Potter
 * December 2010
 *
 * Based on code from NXP app note AN10916.
 */

#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

#define SD_SECTOR_SIZE          512

struct sdcard_config {
	uint32_t size;		// size=sectorsize*sectorcnt
	uint32_t sectorcnt;	// 
	uint32_t blocksize;	// erase block size
	uint8_t ocr[4];
	uint8_t cid[16];
	uint8_t csd[16];
};

extern struct sdcard_config sdcard_config;

/* Card type flags (sdcard_card_type) */
#define CT_NONE                         0x00
#define CT_MMC                          0x01
#define CT_SD1                          0x02
#define CT_SD2                          0x04
#define CT_SDC                          (CT_SD1|CT_SD2)
#define CT_BLOCK                        0x08
extern uint8_t sdcard_card_type;

int sdcard_init(void);
int sdcard_wait_for_ready(void);
int sdcard_read(uint8_t * buf, int sector, int count);
int sdcard_write(const uint8_t * buf, int sector, int count);
int sdcard_get_sd_status(uint8_t * buf);

#endif
