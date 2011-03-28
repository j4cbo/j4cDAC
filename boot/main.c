/* USB DFU bootloader for LPC17xx
 *   Copyright (c) 2011 Jacob Potter <jacob@durbatuluk.us>
 * Based on usboot, USB CDC bootloader for LPC2xxx
 *   Copyright (c) 2008 Dave Madden <dhm@mersenne.com>
 * Based on LPCUSB, a USB device driver for LPC microcontrollers	
 *   Copyright (C) 2006 Bertrik Sikken (bertrik@sikken.nl)
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <minilib.h>
#include <LPC17xx.h>
#include "usbdebug.h"
#include "usboot_iap.h"
#include "usbapi.h"
#include "dfu.h"

#define APP_START	0x4000

volatile uint32_t time = 0;
volatile int st_status;

void SysTick_Handler(void) {
	if (time % 500 == 0) {
		LPC_GPIO0->FIOSET = 1;
		LPC_GPIO1->FIOCLR = (1 << 29);
	} else if (time % 500 == 50) {
		LPC_GPIO0->FIOCLR = 1;
		LPC_GPIO1->FIOSET = (1 << 29);
	}
	time++;
}

#define MAX_PACKET_SIZE		64

#define LE_WORD(x)		((x)&0xFF), ((x)>>8)

#define DFU_FUNCTIONAL	0x21

#define DFU_DETACH	0
#define DFU_DNLOAD	1
#define DFU_UPLOAD	2
#define DFU_GETSTATUS	3
#define DFU_CLRSTATUS	4
#define DFU_GETSTATE	5
#define DFU_ABORT	6

static uint8_t abClassReqData[8];

static const uint8_t abDescriptors[] = {
	/* Device */
	18,
	DESC_DEVICE,
	LE_WORD(0x0101),		// bcdUSB
	0x00,				// bDeviceClass
	0x00,				// bDeviceSubClass
	0x00,				// bDeviceProtocol
	MAX_PACKET_SIZE0,		// bMaxPacketSize
	LE_WORD(0xFFFF),		// idVendor
	LE_WORD(0x0005),		// idProduct
	LE_WORD(0x0100),		// bcdDevice
	0x01,				// iManufacturer
	0x02,				// iProduct
	0x03,				// iSerialNumber
	0x01,				// bNumConfigurations

	/* Configuration */
	0x09,
	DESC_CONFIGURATION,
	LE_WORD(27),			// wTotalLength
	0x01,				// bNumInterfaces
	0x01,				// bConfigurationValue
	0x00,				// iConfiguration
	0xC0,				// bmAttributes
	0x32,				// bMaxPower

	/* DFU interface */
	0x09,
	DESC_INTERFACE,
	0x00,				// bInterfaceNumber
	0x00,				// bAlternateSetting
	0x00,				// bNumEndPoints
	0xFE,				// bInterfaceClass
	0x01,				// bInterfaceSubClass
	0x02,				// bInterfaceProtocol
	0x00,				// iInterface

	/* DFU functional descriptor */
	0x09,
	DFU_FUNCTIONAL,
	0x01,				// bmAttributes
	LE_WORD(100),			// wDetachTimeOut
	LE_WORD(64),			// wTransferSize
	LE_WORD(0x0101),		// wDFUVersion

	// string descriptors
	0x04,
	DESC_STRING,
	LE_WORD(0x0409),		// wLANGID

	0x0E,
	DESC_STRING,
	'L', 0, 'P', 0, 'C', 0, 'U', 0, 'S', 0, 'B', 0,

	36,
	DESC_STRING,
	'j', 0, '4', 0, 'c', 0, 'D', 0, 'A', 0, 'C', 0, ' ', 0,
	'b', 0, 'o', 0, 'o', 0, 't', 0, 'l', 0, 'o', 0, 'a', 0, 'd', 0, 'e', 0, 'r', 0,

	0x12,
	DESC_STRING,
	'D', 0, 'E', 0, 'A', 0, 'D', 0, 'C', 0, '0', 0, 'D', 0, 'E', 0,

// terminating zero
	0
};
struct dfu_getstatus_resp {
	uint8_t status;
	uint8_t poll_time[3];
	uint8_t newstate;
	uint8_t status_string;
} __attribute__((packed));

enum dfu_status last_status = OK;
enum dfu_state state = dfuIDLE;

uint8_t data_buffer[64];
uint8_t write_buffer[256] __attribute__((aligned(4)));
volatile int data_req = 0;
volatile int block = 0;
volatile int reset_flag = 0;
volatile int reset_armed = 0;

static int HandleClassRequest(TSetupPacket *pSetup, int *piLen, uint8_t **ppbData) {
	static struct dfu_getstatus_resp resp;
	static uint8_t state_resp;

	switch (pSetup->bRequest) {
	case DFU_DETACH:
		outputf("> DFU: detach");
		break;

	case DFU_DNLOAD:
		outputf("> DFU: dnload: block %d data %d", pSetup->wValue, *piLen);

		if (*piLen == 0) {
			state = dfuMANIFEST_SYNC;
			break;
		}

		if (*piLen != 64) {
			/* We ask for 64-byte blocks! */
			state = dfuERROR;
			last_status = errADDRESS;
			break;
		}

		memcpy(data_buffer, *ppbData, *piLen);
		block = pSetup->wValue;
		data_req = 1;
		state = dfuDNLOAD_SYNC;
		break;

	case DFU_UPLOAD:
		outputf("> DFU: upload");
		return FALSE;

	case DFU_GETSTATUS:
		outputf("> DFU: getting status: state is %d", state);
		reset_armed = 1;
		switch (state) {
		case dfuDNLOAD_SYNC:
			if (data_req) {
				resp.newstate = dfuDNBUSY;
			} else {
				state = dfuDNLOAD_IDLE;
				resp.newstate = state;
			}
			break;
		case dfuMANIFEST_SYNC:
			state = dfuIDLE;
			resp.newstate = state;
			break;
		default:
			resp.newstate = state;
		}

		resp.status = last_status;
		resp.poll_time[0] = 20;
		resp.poll_time[1] = 0;
		resp.poll_time[2] = 0;
		resp.status_string = 0;

		*ppbData = (uint8_t *)&resp;
		*piLen = 6;
		break;

	case DFU_CLRSTATUS:
		outputf("> DFU: clearing status");
		last_status = OK;
		break;

	case DFU_ABORT:
		outputf("> DFU: abort");
		state = dfuIDLE;
		break;

	case DFU_GETSTATE:
		outputf(">DFU: getstate");
		state_resp = state;
		*ppbData = &state_resp;
		*piLen = 1;
		break;

	default:
		outputf(">DFU: unk %d", pSetup->bRequest);
		return FALSE;
	}
	return TRUE;
}

/**
        USB reset handler
        
        @param [in] bDevStatus  Device status
 */
void HandleUsbReset(unsigned char bDevStatus)
{
	outputf("bDevStatus %d", bDevStatus);
        if (bDevStatus & DEV_STATUS_RESET) {
		if (reset_armed)
			reset_flag = 1;
        }
}

void enter_app(void * sp, void * pc);

extern uint32_t crc32(uint32_t crc, unsigned char *buf, int len);

int app_ok() {
	void **app_vectors = (void **)APP_START;

	int app_length = (int)app_vectors[8];

	outputf("App length: %d", app_length);
	if (app_length < 1024) {
		return 0;
	}

	uint32_t expected_crc = *(uint32_t *)(APP_START + app_length);
	uint32_t crc = crc32(0, (unsigned char *)APP_START, app_length);

	outputf("CRC: expected 0x%08x, got 0x%08x", expected_crc, crc);

	return (crc == expected_crc);
}

void enter_application() {

	outputf("Entering application...");

	/*
	 * Return CPU to reset state
	 */
	__disable_irq();

	/* Move interrupts to the application section */
	SCB->VTOR = APP_START;

	void **app_vectors = (void **)APP_START;

	/* Enter the application */
	enter_app(app_vectors[0], app_vectors[1]);
}

/*************************************************************************
	main
	====
**************************************************************************/
int main(void)
{
	SysTick_Config(SystemCoreClock / 1000);

        serial_init();

	outputf("== BOOTLOADER ==");

	/* Sample the bootloader pin to see if it is being forced high; if
	 * so, or if the application is not intact, enter bootload mode. */
	LPC_PINCON->PINMODE4 |= (3 << 20);
	LPC_PINCON->PINMODE3 |= (3 << 30);

	/* Wait a bit */
	int i;
	for (i = 0; i < 10; i++) {
		asm volatile("nop");
	}

	int pin_held = (LPC_GPIO2->FIOPIN & (1 << 10)) ? 1 : 0;
	int app_is_ok = app_ok();

	outputf("Force bootloader: %d; app integrity: %d", pin_held, app_is_ok);

	/* Check whether we need to enter application mode */
	if (!pin_held && app_is_ok) {
		enter_application();
	}

	/* Move interrupts into RAM */
	SCB->VTOR = 0x10000000;

	/* Set up LEDs */
	LPC_GPIO0->FIODIR |= (1 << 0);
	LPC_GPIO1->FIODIR |= (1 << 29);
	LPC_GPIO1->FIODIR |= (1 << 25);

	// initialise stack
	usb_init();

	// register descriptors
	USBRegisterDescriptors(abDescriptors);

	// register class request handler
	USBRegisterRequestHandler(REQTYPE_TYPE_CLASS, HandleClassRequest, abClassReqData);

	// enable bulk-in interrupts on NAKs
	USBHwNakIntEnable(INACK_BI);

	outputf("Starting USB communication\n");

	// set up USB interrupt
        NVIC_SetPriority( USB_IRQn, 3 );
        NVIC_EnableIRQ( USB_IRQn );

	__enable_irq();

	// connect to bus
	USBHwConnect(TRUE);

	uint32_t expected_length;

	unsigned long sector_base = 0;
	unsigned long sector_len = 0;

	// Run the app.
	while(1) {
		while (!data_req && !reset_flag);
		if (reset_flag) {
			if (app_ok()) break;
			reset_flag = 0;
		}

		if (block == 0) {
			if (memcmp((char *)data_buffer, "j4cDAC", 6)) {
				state = errTARGET;
				data_req = 0;
				continue;
			}

			data_req = 0;
			continue;
		}

		block--;

		if (block == 0) {
			/* Image length in vectors */ 
			expected_length = *(uint32_t *)(data_buffer + 0x20);
		}

		int base = (block & ~3) * 64 + APP_START;

		if (base < sector_base ||
		    base + 256 > sector_base + sector_len) {
			if (iapFindSector(base, &sector_base, &sector_len) > 0) {
				outputf("# Erase 0x%x [0x%x]", sector_base, sector_len);
				int result = iapErase(sector_base, sector_len, SystemCoreClock / 1000);
				if (result != IAP_CMD_SUCCESS) {
					outputf("!!! Erase failed: %d", result);
					state = errERASE;
					data_req = 0;
					continue;
				}
			} else {
				outputf("!!! Can't find sector for 0x%x", base);
				state = errADDRESS;
				data_req = 0;
				continue;
			}
		}

		/* Copy into write buffer */
		memcpy(write_buffer + (64 * (block % 4)), data_buffer, 64);

		if (block % 4 != 3) {
			/* Don't write until we have all of this block */
			data_req = 0;
			continue;
		}

		outputf("# Write 0x%x ... 0x%x", base, base + 256);
		int result = iapWrite(base, write_buffer, 256, SystemCoreClock / 1000);

		if (result != IAP_CMD_SUCCESS) {
			outputf("!!! Write failed: %d", result);
			state = errWRITE;
			data_req = 0;
			continue;
		}

		outputf("# Write done");
		data_req = 0;
	}

	USBHwConnect(FALSE);

	enter_application();

	/* Unreachable. */
	return 0;
}
