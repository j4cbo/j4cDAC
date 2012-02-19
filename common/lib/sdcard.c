/* LPC1758 SD driver
 *
 * Jacob Potter
 * December 2010
 *
 * Based on code from NXP app note AN10916.
 */

#include <stdint.h>
#include <attrib.h>
#include "sdcard.h"

#include <hardware.h>
#include <serial.h>

#define ERR_READY_TIMEOUT	-1
#define ERR_RECVBLOCK_TIMEOUT	-2

/* SD/MMC Commands */
#define GO_IDLE_STATE    	(0x40 + 0)	// CMD0
#define SEND_OP_COND     	(0x40 + 1)
#define SEND_IF_COND		(0x40 + 8)	// CMD8
#define SEND_CSD         	(0x40 + 9)
#define SEND_CID         	(0x40 + 10)	// CMD10
#define STOP_TRAN        	(0x40 + 12)	// CMD12
#define SET_BLOCKLEN     	(0x40 + 16)	// CMD16
#define READ_BLOCK       	(0x40 + 17)
#define READ_MULT_BLOCK  	(0x40 + 18)
#define WRITE_BLOCK      	(0x40 + 24)
#define WRITE_MULT_BLOCK 	(0x40 + 25)
#define APP_CMD         	(0x40 + 55)	// CMD55
#define READ_OCR	 	(0x40 + 58)	// CMD58
#define CRC_ON_OFF       	(0x40 + 59)
#define SD_SEND_OP_COND 	(0xC0 + 41)	// ACMD41
#define SD_STATUS		(0xC0 + 13)	// ACMD13, SD_STATUS (SDC)
#define SET_WR_BLK_ERASE_COUNT	(0xC0 + 23)	// ACMD23 (SDC)

/* token for write operation */
#define TOKEN_SINGLE_BLOCK	0xFE
#define TOKEN_MULTI_BLOCK	0xFC
#define TOKEN_STOP_TRAN		0xFD

static uint16_t ffff = 0xffff;

static uint8_t sdcard_spi_recv(void);
// static void sdcard_spi_recvblock(uint8_t * b, int l);
static void sdcard_spi_dma_recvblock(uint8_t * b, int l);
static void sdcard_spi_send(uint8_t x);
static void sdcard_spi_sendblock(const uint8_t * b, int l);
static void sdcard_spi_select();
static void sdcard_spi_deselect();
static void sdcard_spi_init();
void sdcard_spi_set_speed(int speed);

struct sdcard_config sdcard_config;
uint8_t sdcard_card_type;

int sdcard_wait_for_ready(void) {
	int timeout = 500000;

	sdcard_spi_send(0xFF);

	while (timeout--) {
		if (sdcard_spi_recv() == 0xFF)
			return 0;
	}

	return ERR_READY_TIMEOUT;
}

static uint8_t sdcard_send_cmd(uint8_t command, uint32_t arg) {
	uint8_t res;
	int n;

	/* This command may be an ACMD value - if so, it is sent
	 * prefixed with APP_CMD. */
	if (command & 0x80) {
		command &= 0x7F;
		res = sdcard_send_cmd(APP_CMD, 0);
		if (res > 1)
			return res;
	}

	/* "Hey, card!" */
	sdcard_spi_deselect();
	sdcard_spi_select();

	if ((res = sdcard_wait_for_ready()) < 0)
		return res;

	/* Send the command and parameter */
	sdcard_spi_send(0xFF);
	sdcard_spi_send(command);
	sdcard_spi_send(arg >> 24);
	sdcard_spi_send(arg >> 16);
	sdcard_spi_send(arg >> 8);
	sdcard_spi_send(arg);

	/* We need to provide valid CRCs during initialization. */
	if (command == GO_IDLE_STATE)
		sdcard_spi_send(0x95);
	else if (command == SEND_IF_COND)
		sdcard_spi_send(0x87);
	else
		sdcard_spi_send(0);

	/* Skip a byte for STOP_TRAN */
	if (command == STOP_TRAN)
		sdcard_spi_recv();

	/* Try 10 times for a response */
	n = 10;
	do {
		res = sdcard_spi_recv();
	} while ((res & 0x80) && --n);

	return res;
}

static int sdcard_read_block(uint8_t * buf, uint32_t len) {
	uint8_t token;
	uint32_t timeout;

	/* Wait for the card to have data for us */
	timeout = 20000;
	do {
		token = sdcard_spi_recv();
	} while ((token == 0xFF) && timeout--);

	if (token != 0xFE)
		return ERR_RECVBLOCK_TIMEOUT;

	sdcard_spi_dma_recvblock(buf, len);

	/* Ignore CRC. */
	sdcard_spi_recv();
	sdcard_spi_recv();

	return 0;
}

/* sdcard_write_block
 *
 * Send a 512-byte block of data to the card. Data blocks are preceded by
 * a token, which we send first.
 */
static int sdcard_write_block(const uint8_t * buf, uint8_t token) {
	uint8_t resp;
	int res;

	sdcard_spi_send(token);

	/* Bail out now? */
	if (token == TOKEN_STOP_TRAN)
		return 0;

	sdcard_spi_sendblock(buf, 512);
	/* Ignore CRC */
	sdcard_spi_recv();
	sdcard_spi_recv();

	/* Get response */
	resp = sdcard_spi_recv();
	if ((resp & 0x1F) != 0x05)
		return -1;

	/* Wait for write to complete */
	if ((res = sdcard_wait_for_ready()) < 0)
		return res;

	return 0;
}

/* Read MMC/SD Card device configuration. */
int sdcard_read_config(struct sdcard_config *cfg) {
	uint16_t csize;
	int i, ret;
	uint8_t csd[16], n;

	/* Read the OCR - Operations Condition Register. */
	if (sdcard_send_cmd(READ_OCR, 0))
		goto bailout;
	for (i = 0; i < 4; i++)
		cfg->ocr[i] = sdcard_spi_recv();

	/* Read the CID - Card Identification. */
	if (sdcard_send_cmd(SEND_CID, 0) != 0)
		goto bailout;
	if ((ret = sdcard_read_block(cfg->cid, 16)) < 0)
		goto bailout;

	/* Read the CSD - Card Specific Data. */
	if (sdcard_send_cmd(SEND_CSD, 0) != 0)
		goto bailout;
	if ((ret = sdcard_read_block(cfg->csd, 16)) < 0)
		goto bailout;

	/* Get number of sectors on the disk (DWORD) */
	if ((cfg->csd[0] >> 6) == 1) {
		/* SD v2 */
		csize = cfg->csd[9] + ((uint16_t) cfg->csd[8] << 8) + 1;
		cfg->sectorcnt = (uint32_t) csize << 10;
	} else {
		/* SD v1 or MMC */
		int n = ((cfg->csd[5] & 15)
			 + ((cfg->csd[10] & 128) >> 7)
			 + ((cfg->csd[9] & 3) << 1) + 2);
		csize = (cfg->csd[8] >> 6) + ((uint16_t) cfg->csd[7] << 2)
		    + ((uint16_t) (cfg->csd[6] & 3) << 10) + 1;
		cfg->sectorcnt = (uint32_t) csize << (n - 9);
	}

	cfg->size = cfg->sectorcnt * SD_SECTOR_SIZE;

	/* Get erase block size in unit of sector (DWORD) */
	if (sdcard_card_type & CT_SD2) {
		/* SD v2 */
		if (sdcard_send_cmd(SD_STATUS, 0) != 0)
			goto bailout;

		sdcard_spi_recv();
		if (sdcard_read_block(csd, 16) < 0)
			goto bailout;

		/* Purge trailing data */
		for (n = 64 - 16; n; n--)
			sdcard_spi_recv();
		cfg->blocksize = 16UL << (csd[10] >> 4);
		return 0;
	} else {
		/* SD v1 or MMC */
		if (sdcard_send_cmd(SEND_CSD, 0) != 0)
			goto bailout;

		if (sdcard_read_block(csd, 16) < 0)
			goto bailout;

		if (sdcard_card_type & CT_SD1) {
			/* SD v1 */
			cfg->blocksize = (((csd[10] & 63) << 1) +
					  ((uint16_t) (csd[11] & 128) >> 7)
					  + 1) << ((csd[13] >> 6) - 1);
		} else {
			/* MMC */
			cfg->blocksize =
			    ((uint16_t) ((cfg->csd[10] & 124) >> 2) + 1)
			    * (((cfg->csd[10] & 3) << 3)
			       + ((cfg->csd[11] & 224) >> 5) + 1);
		}
		return 0;
	}

bailout:
	sdcard_spi_deselect();
	sdcard_spi_recv();
	return -1;
}

int COLD sdcard_init(void) {
	uint32_t i, timeout;
	uint8_t cmd, ct, ocr[4];

	/* Initialize SPI interface and enable Flash Card SPI mode. */
	sdcard_spi_init();

	/* According to the SD spec, we must give the card at least 74
	 * clock cycles before talking to it. */
	for (i = 0; i < 20; i++) {
		sdcard_spi_recv();
	}

	if (sdcard_send_cmd(GO_IDLE_STATE, 0) != 1)
		return -1;
	if (sdcard_send_cmd(SEND_IF_COND, 0x1AA) != 1)
		return -1;

	timeout = 50000;

	/* Get trailing return value of R7 resp */
	for (i = 0; i < 4; i++)
		ocr[i] = sdcard_spi_recv();

	if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
		/* SDHC */
		/* Wait for leaving idle state (ACMD41 with HCS bit) */
		while (timeout--
		       && sdcard_send_cmd(SD_SEND_OP_COND, 1UL << 30));

		/* Check CCS bit in the OCR */
		if (timeout && sdcard_send_cmd(READ_OCR, 0) == 0) {
			for (i = 0; i < 4; i++)
				ocr[i] = sdcard_spi_recv();
			ct = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
		} else {
			return -1;
		}
	} else {
		/* SDSC or MMC */
		if (sdcard_send_cmd(SD_SEND_OP_COND, 0) <= 1) {
			/* SD */
			ct = CT_SD1;
			cmd = SD_SEND_OP_COND;
		} else {
			/* MMC */
			ct = CT_MMC;
			cmd = SEND_OP_COND;
		}

		/* Wait for leaving idle state */
		while (timeout-- && sdcard_send_cmd(cmd, 0));

		/* Set R/W block length to 512 */
		if (!timeout
		    || sdcard_send_cmd(SET_BLOCKLEN, SD_SECTOR_SIZE) != 0)
			return -1;
	}

	sdcard_card_type = ct;
	sdcard_spi_deselect();
	sdcard_spi_recv();

	/* And up to full speed! */
	sdcard_spi_set_speed(20000000);

	/* Now that we've initialized the card, let's read its configuration. */
	return sdcard_read_config(&sdcard_config);
}

int sdcard_read(uint8_t * buf, int sector, int count) {
	int ret;

	/* Convert to byte address if needed */
	if (!(sdcard_card_type & CT_BLOCK))
		sector *= SD_SECTOR_SIZE;

	if (count == 1) {
		/* Single block read */
		if ((ret = sdcard_send_cmd(READ_BLOCK, sector)) != 0)
			return -1;

		if ((ret = sdcard_read_block(buf, SD_SECTOR_SIZE)) < 0)
			return -1;

		return 0;
	} else {
		/* Multiple block read */
		if ((ret = sdcard_send_cmd(READ_MULT_BLOCK, sector)) != 0)
			return -1;

		while (count --> 0) { 
			if ((ret = sdcard_read_block(buf, SD_SECTOR_SIZE)) < 0)
				return ret;
			buf += SD_SECTOR_SIZE;
		}

		sdcard_send_cmd(STOP_TRAN, 0);
	}

	sdcard_spi_deselect();
	sdcard_spi_recv();
	return 0;
}

int sdcard_write(const uint8_t * buf, int sector, int count) {
	int ret;

	/* Convert to byte address if needed */
	if (!(sdcard_card_type & CT_BLOCK))
		sector *= SD_SECTOR_SIZE;

	if (count == 1) {
		/* Single block write */
		if ((ret = sdcard_send_cmd(WRITE_BLOCK, sector)) != 0)
			return -1;

		if ((ret = sdcard_write_block(buf, TOKEN_SINGLE_BLOCK)) < 0)
			return -1;

		return 0;
	} else {
		/* Multiple block write */
		if (sdcard_card_type & CT_SDC)
			sdcard_send_cmd(SET_WR_BLK_ERASE_COUNT, count);

		if ((ret = sdcard_send_cmd(WRITE_MULT_BLOCK, sector)) != 0)
			return -1;

		while (count --> 0) {
			if ((ret = sdcard_write_block(buf, TOKEN_MULTI_BLOCK)) < 0)
				return -1;
			buf += SD_SECTOR_SIZE;
		}

		if ((ret = sdcard_write_block(0, TOKEN_STOP_TRAN)) < 0) {
			return ret;
		}
	}

	sdcard_spi_deselect();
	sdcard_spi_recv();
	return 0;
}

int sdcard_get_sd_status(uint8_t * buf) {
	int ret;

	if (sdcard_send_cmd(SD_STATUS, 0) != 0)
		return -1;

	sdcard_spi_recv();

	if ((ret = sdcard_read_block(buf, 64)) < 0)
		return ret;

	sdcard_spi_deselect();
	sdcard_spi_recv();
	return 0;
}

#define SS_PROTO_P0_PIN	(1 << 22)
#define SS_PROD_P1_PIN	(1 << 25)

void COLD sdcard_spi_init(void) {
	int dummy;

	/* Configure I/O pins */
	/* SSEL is GPIO, output set to high. */
	if (hw_board_rev == HW_REV_PROTO)
		LPC_GPIO0->FIODIR |= SS_PROTO_P0_PIN;
	else
		LPC_GPIO1->FIODIR |= SS_PROD_P1_PIN;
	sdcard_spi_deselect();

	/* SCK, MISO, MOSI are SSP pins. */
	if (hw_board_rev == HW_REV_PROTO) {
		LPC_PINCON->PINSEL0 &= ~(3 << 30);
		LPC_PINCON->PINSEL0 |= (2 << 30);
		LPC_PINCON->PINSEL1 &= ~((3 << 2) | (3 << 4));
		LPC_PINCON->PINSEL1 |= ((2 << 2) | (2 << 4));
	} else {
		LPC_PINCON->PINSEL3 |= ((3 << 8) | (3 << 14) | (3 << 16));
	}

	/* PCLK_SSP0=CCLK */
	LPC_SC->PCLKSEL1 &= ~(3 << 10);	/* PCLKSP0 = CCLK/4 (18MHz) */
	LPC_SC->PCLKSEL1 |= (1 << 10);	/* PCLKSP0 = CCLK   (72MHz) */

	LPC_SSP0->CR0 = SSP_CR0_DSS_8;
	LPC_SSP0->CR1 = SSP_CR1_SSP_EN;

	sdcard_spi_set_speed(400000);

	/* wait for busy gone */
	while (LPC_SSP0->SR & SSP_SR_BSY);

	/* drain SPI RX FIFO */
	while (LPC_SSP0->SR & SSP_SR_RNE)
		dummy = LPC_SSP0->DR;

	/* Set up DMA controller */
	LPC_GPDMA->DMACConfig = 1;
	while (!(LPC_GPDMA->DMACConfig & 1));
	outputf("GPDMA DMACConfig: 0x%08x\n", LPC_GPDMA->DMACConfig);
	LPC_GPDMACH0->DMACCConfig = DMACC_Config_M2P
		| DMACC_Config_DestPeripheral_SSP0Tx;
	LPC_GPDMACH1->DMACCConfig = DMACC_Config_P2M
		| DMACC_Config_SrcPeripheral_SSP0Rx;
	LPC_GPDMACH0->DMACCLLI = 0;
	LPC_GPDMACH1->DMACCLLI = 0;
	LPC_SSP0->DMACR |= 3;
	LPC_GPDMACH0->DMACCSrcAddr = (uint32_t)&ffff;
	LPC_GPDMACH0->DMACCDestAddr = (uint32_t)&LPC_SSP0->DR;
	LPC_GPDMACH1->DMACCSrcAddr = (uint32_t)&LPC_SSP0->DR;
}

/* Set SSP0 clock speed to desired value. */
void sdcard_spi_set_speed(int speed) {
	int cpsr = SystemCoreClock / speed;

	if (cpsr < 2)
		cpsr = 2;
	if (cpsr > 254)
		cpsr = 254;

	LPC_SSP0->CPSR = cpsr;
}

static void sdcard_spi_select(void) {
	if (hw_board_rev == HW_REV_PROTO)
		LPC_GPIO0->FIOCLR = SS_PROTO_P0_PIN;
	else
		LPC_GPIO1->FIOCLR = SS_PROD_P1_PIN;
}

static void sdcard_spi_deselect(void) {
	if (hw_board_rev == HW_REV_PROTO)
		LPC_GPIO0->FIOSET = SS_PROTO_P0_PIN;
	else
		LPC_GPIO1->FIOSET = SS_PROD_P1_PIN;
}

static uint8_t sdcard_spi_exchange_byte(uint8_t send) {
	LPC_SSP0->DR = send;

	/* Wait for transfer to finish */
	while (LPC_SSP0->SR & SSP_SR_BSY);

	return LPC_SSP0->DR;
}

static void sdcard_spi_send(uint8_t data) {
	sdcard_spi_exchange_byte(data);
}

static uint8_t sdcard_spi_recv(void) {
	return sdcard_spi_exchange_byte(0xFF);
}

static void sdcard_spi_dma_recvblock(uint8_t *buf, int len) {

	/* Set up channel 0 to copy ffff into DR */
	LPC_GPDMACH0->DMACCControl = len | DMACC_Control_SWIDTH_8 \
		| DMACC_Control_DWIDTH_8 \
		| DMACC_Control_SBSIZE_4 | DMACC_Control_DBSIZE_4;

	/* Set up channel 1 to copy dr to buf */
	LPC_GPDMACH1->DMACCDestAddr = (uint32_t)buf;
	LPC_GPDMACH1->DMACCControl = len | DMACC_Control_SWIDTH_8 \
		| DMACC_Control_DWIDTH_8 | DMACC_Control_DI \
		| DMACC_Control_SBSIZE_4 | DMACC_Control_DBSIZE_4;

	/* Enable channels */
	__disable_irq();
	LPC_GPDMACH0->DMACCConfig |= DMACC_Config_E;
	LPC_GPDMACH1->DMACCConfig |= DMACC_Config_E;
	__enable_irq();

	/* Wait... */
	int count = len * 50;
	while(count-- && 
	      ((LPC_GPDMACH0->DMACCConfig & DMACC_Config_E)
	    || (LPC_GPDMACH1->DMACCConfig & DMACC_Config_E)));

	if (!count)
		serial_send_str("SD timeout\r\n");
}

#if 0
#define FIFO_ELEM 8

void sdcard_spi_recvblock(uint8_t * buf, int len) {
	int i, startcnt, nwords = len / 2;

	if (len < FIFO_ELEM) {
		startcnt = nwords;
	} else {
		startcnt = FIFO_ELEM;
	}

	/* Set the SSP peripheral to 16-bit mode */
	LPC_SSP0->CR0 = SSP_CR0_DSS_16;

	/* fill TX FIFO, prepare clk for receive */
	for (i = 0; i < startcnt; i++) {
		LPC_SSP0->DR = 0xffff;
	}

	i = 0;

	do {
		// wait for data in RX FIFO (RNE set)
		while (!(LPC_SSP0->SR & SSP_SR_RNE));

		uint32_t rec = LPC_SSP0->DR;

		if (i < (nwords - startcnt)) {
			/* fill the TX fifo up again */
			LPC_SSP0->DR = 0xffff;
		}

		*buf++ = (uint8_t) (rec >> 8);
		*buf++ = (uint8_t) (rec);
		i++;
	} while (i < nwords);

	/* Back to 8-bit mode */
	LPC_SSP0->CR0 = SSP_CR0_DSS_8;
}
#endif

/* Send a block of data. */
static void sdcard_spi_sendblock(const uint8_t * buf, int len) {
	uint32_t cnt;
	uint16_t data;

	/* Set the SSP peripheral to 16-bit mode */
	LPC_SSP0->CR0 = SSP_CR0_DSS_16;

	/* fill the FIFO unless it is full */
	for (cnt = 0; cnt < (len / 2); cnt++) {
		/* wait for TX FIFO not full (TNF) */
		while (!(LPC_SSP0->SR & SSP_SR_TNF));

		data = (*buf++) << 8;
		data |= *buf++;
		LPC_SSP0->DR = data;
	}

	/* wait for BSY gone */
	while (LPC_SSP0->SR & SSP_SR_BSY);

	/* drain receive FIFO */
	while (LPC_SSP0->SR & SSP_SR_RNE) {
		data = LPC_SSP0->DR;
	}

	/* Back to 8-bit mode */
	LPC_SSP0->CR0 = SSP_CR0_DSS_8;
}
