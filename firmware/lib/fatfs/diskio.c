/* Adapter between SD card driver and FatFs interface
 *
 * Jacob Potter
 * December 2010
 *
 * Based on code from NXP app note AN10916.
 */

#include "ff.h"
#include "diskio.h"
#include <sdcard.h>
#include <string.h>

static volatile BYTE Stat = STA_NOINIT;

/* disk_initialize
 *
 * Set up the disk.
 */
DSTATUS disk_initialize (BYTE drv) {
	if (drv == 0 && sdcard_init() == 0) {
		Stat &= ~STA_NOINIT;
	}

	return Stat;
}

/* disk_read
 *
 * Read some sectors.
 */
DRESULT disk_read (BYTE drv, BYTE *buf, DWORD sector, BYTE count) {
	if (drv || !count) return RES_PARERR;
	if (Stat & STA_NOINIT) return RES_NOTRDY;
	if (sdcard_read(buf, sector, count) == 0)	
		return RES_OK;
	else
		return RES_ERROR;
}

/* disk_write
 *
 * Write some sectors.
 */
DRESULT disk_write (BYTE drv, const BYTE *buf,	DWORD sector, BYTE count) {
	if (drv || !count) return RES_PARERR;
	if (Stat & STA_NOINIT) return RES_NOTRDY;
	if (sdcard_write(buf, sector, count) == 0)
		return RES_OK;
	else
		return 	RES_ERROR;
}

/* disk_status
 *
 * Check the status of this drive. All we know how to say is "initialized"
 * vs "uninitialized".
 */
DSTATUS disk_status (BYTE drv) {
	if (drv) return STA_NOINIT;
	return Stat;
}

/* disk_ioctl
 *
 * Everything else.
 */
DRESULT disk_ioctl (BYTE drv, BYTE ctrl, void *buf) {
	DRESULT res;

	if (drv) return RES_PARERR;
	if (Stat & STA_NOINIT) return RES_NOTRDY;

	res = RES_ERROR;

	switch (ctrl) {
	case CTRL_SYNC:
		/* Make sure that no pending write process */
		if (sdcard_wait_for_ready() == 0)
			res = RES_OK;
		break;

	case GET_SECTOR_COUNT:
		/* Get number of sectors on the disk (DWORD) */
		*(DWORD*)buf = sdcard_config.sectorcnt;
		res = RES_OK;
		break;

	case GET_SECTOR_SIZE:
		/* Get R/W sector size (WORD) */
		*(WORD*)buf = 512;
		res = RES_OK;
		break;

	case GET_BLOCK_SIZE:
		/* Get erase block size in unit of sector (DWORD) */
		*(DWORD*)buf = sdcard_config.blocksize;
		res = RES_OK;
		break;

	case MMC_GET_TYPE:
		/* Get card type flags (1 byte) */
		*(BYTE*)buf = sdcard_card_type;
		res = RES_OK;
		break;

	case MMC_GET_CSD:
		/* Receive CSD as a data block (16 bytes) */
		memcpy(buf, sdcard_config.csd, sizeof(sdcard_config.csd));
		res = RES_OK;
		break;

	case MMC_GET_CID:
		/* Receive CID as a data block (16 bytes) */
		memcpy(buf, sdcard_config.cid, sizeof(sdcard_config.cid));
		res = RES_OK;
		break;

	case MMC_GET_OCR:
		/* Receive OCR as an R3 resp (4 bytes) */
		memcpy(buf, sdcard_config.ocr, sizeof(sdcard_config.ocr));
		res = RES_OK;
		break;

	case MMC_GET_SDSTAT:
		/* Receive SD status as a data block (64 bytes) */
		if (sdcard_get_sd_status(buf) == 0)
			res = RES_OK;
		break;

	default:
		res = RES_PARERR;
	}

	return res;
}
