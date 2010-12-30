/* j4cDAC test code
 *
 * Copyright 2010 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <LPC17xx.h>
#include <diskio.h>
#include <ff.h>
#include <serial.h>
#include "ether.h"
#include <lwip/init.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>
#include <string.h>
#include <dac.h>
#include <assert.h>

#include <lpc17xx_gpdma.h>
#include <lpc17xx_timer.h>

volatile uint32_t time;
volatile uint32_t halfsecond;

FATFS fs;
char filename_buf[512];

#define DEBUG_UART	((LPC_UART_TypeDef *)LPC_UART0)

extern const unsigned char ildatest_bts[7164];

void SysTick_Handler(void) {
	time++;
	if ((time % 500) == 0)
		halfsecond = 1;
}

void delay_ms(uint16_t length) {
	uint32_t end = time + length;
	while (time < end);
}

void HardFault_Handler_C(uint32_t * stack) {
	outputf("*** HARD FAULT ***");
	outputf("pc: %p", stack[6]);
	while (1);
}

void BusFault_Handler_C(uint32_t * stack) {
	outputf("*** BUS FAULT ***");
	outputf("pc: %p", stack[6]);
	while (1);
}

void sd_init(void) {
	int res = disk_initialize(0);
	if (res) {
		outputf("SD initialization failed: %d", res);
		return;
	}

	/* This code sucks. It comes from the fatfs example code. */
	memset(&fs, 0, sizeof(fs));
	res = f_mount(0, &fs);
	if (res) {
		outputf("f_mount() failed: %d", res);
		return;
	}

	DIR dir;
	res = f_opendir(&dir, "");
	if (res)
		return;

	FILINFO finfo;
	int num_subdirs = 0, num_files = 0, total_size = 0;

	while (1) {
		finfo.lfname = filename_buf;
		finfo.lfsize = sizeof(filename_buf);
		res = f_readdir(&dir, &finfo);
		if ((res != FR_OK) || !finfo.fname[0])
			break;

		if (finfo.fattrib & AM_DIR) {
			num_subdirs++;
		} else {
			num_files++;
			total_size += finfo.fsize;
		}

		outputf("%c%c%c%c%c %u/%02u/%02u %02u:%02u %9lu  %s  %s",
			(finfo.fattrib & AM_DIR) ? 'D' : '-',
			(finfo.fattrib & AM_RDO) ? 'R' : '-',
			(finfo.fattrib & AM_HID) ? 'H' : '-',
			(finfo.fattrib & AM_SYS) ? 'S' : '-',
			(finfo.fattrib & AM_ARC) ? 'A' : '-',
			(finfo.fdate >> 9) + 1980, (finfo.fdate >> 5) & 15,
			finfo.fdate & 31, (finfo.ftime >> 11),
			(finfo.ftime >> 5) & 63, finfo.fsize,
			&(finfo.fname[0]), filename_buf);
	}
	outputf("%4u File(s),%10lu bytes total\n%4u Dir(s)",
		num_files, total_size, num_subdirs);
}

int main(int argc, char **argv) {
	time = 0;

	uint32_t last_tcp_tick = 0, last_dhcp_coarse = 0, last_dhcp_fine = 0;

	SysTick_Config(SystemCoreClock / 1000);
	serial_init();

	/* LEDs */
	LPC_GPIO0->FIODIR |= (1 << 0);
	LPC_GPIO1->FIODIR |= (1 << 29);

	__enable_irq();

	outputf("=== j4cDAC ===");

	outputf("dac_init()");
	dac_init();

	outputf("lwip_init()");
	lwip_init();

	outputf("eth_init()");
	eth_init();

	outputf("sd_init()");
	sd_init();

	outputf("Entering main loop...");

	ASSERT_EQUAL(dac_prepare(), 0);
	int status = 0;
	int ctr = 0;

	while (1) {
		dac_point_t *ptr = 0;
		int len = dac_request(&ptr);
		if (len < 0) {
			outputf("*** UNDERFLOW ***");
			dac_prepare();
		}

		if (len >= 0 && len < 10 && dac_get_state() != DAC_PLAYING) {
			dac_start(30000);
		}
		 if (len > 0) {

			int i;
			for (i = 0; i < len;) {
				const unsigned char *w = &ildatest_bts[ctr];
				uint16_t x = ((w[3] << 8) & 0xF000) | (w[4] << 4);
				uint16_t y = ((w[3] << 12) & 0xF000) | (w[5] << 4);
				ptr[i].x = x;
				ptr[i].y = y;
				ptr[i].r = w[0] << 4;
				ptr[i].g = w[1] << 4;
				ptr[i].b = w[2] << 4;
				ptr[i].i = 0;
				ptr[i].u1 = 0;
				ptr[i].u2 = 0;

				i++;
				ctr += 6;

				if (ctr >= 7164)
					ctr = 0;
			}
			dac_advance(len);
		}

		if (!(LPC_GPIO1->FIOPIN & (1 << 26))) {
			outputf("Blocking...");
			while (!(LPC_GPIO1->FIOPIN & (1 << 26)));
		}

		if (status) {
			LPC_GPIO0->FIOPIN = 1;
			LPC_GPIO1->FIOPIN = 0;
			status = 0;
		} else {
			LPC_GPIO0->FIOPIN = 0;
			LPC_GPIO1->FIOPIN = (1 << 29);
			status = 1;
		}

		if (time > (last_tcp_tick + 250)) {
			outputf("=== TCP TICK ===");
			tcp_tmr();
			last_tcp_tick += 250;
		}

		if (time > (last_dhcp_coarse + 60000)) {
			outputf("=== DHCP COARSE TICK ===");
			dhcp_coarse_tmr();
			last_dhcp_coarse += 60000;
		}

		if (time > (last_dhcp_fine + 500)) {
			outputf("=== DHCP FINE TICK ===");
			dhcp_fine_tmr();
			last_dhcp_fine += 500;
		}

		eth_poll();
	}
}
