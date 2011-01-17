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
#include <ipv4/lwip/autoip.h>
#include <string.h>
#include <bpm.h>
#include <osc.h>
#include <dac.h>
#include <assert.h>
#include <attrib.h>
#include <broadcast.h>
#include <point-stream.h>
#include <skub.h>
#include <usbapi.h>

#include <lpc17xx_gpdma.h>
#include <lpc17xx_timer.h>

volatile uint32_t time;
volatile uint32_t mtime;

FATFS fs;
char filename_buf[256];

#define DEBUG_UART	((LPC_UART_TypeDef *)LPC_UART0)

extern void tp_trianglewave_run(void);

void SysTick_Handler(void) {
	mtime++;
	if (mtime % 10 == 0) time++;
}

void delay_ms(uint16_t length) {
	uint32_t end = time + length;
	while (time < end);
}

void HardFault_Handler_C(uint32_t * stack) ATTR_VISIBLE;
void HardFault_Handler_C(uint32_t * stack) {
	outputf("*** HARD FAULT ***");
	outputf("stack: %p", stack);
	int i;
	for (i = 0; i < 32; i++) {
		outputf("stack[%d]: %p", i, stack[i]);
	}
	while (1);
}

void BusFault_Handler_C(uint32_t * stack) ATTR_VISIBLE;
void BusFault_Handler_C(uint32_t * stack) {
	outputf("*** BUS FAULT ***");
	outputf("pc: %p", stack[6]);
	while (1);
}

struct periodic_event {
	void (*f)(void);
	int period;
	const char *msg;
	int start;
} const events[] = {
	{ tcp_tmr, 250, "tcp", 100 },
	{ dhcp_coarse_tmr, 60000, "dhcp c", 35 },
	{ dhcp_fine_tmr, 500, "dhcp f", 25 },
	{ autoip_tmr, AUTOIP_TMR_INTERVAL, "autoip", 10 },
	{ broadcast_send, 1000, "broadcast", 10 }
};

int events_last[sizeof(events) / sizeof(events[0])];

void sink_init(void);

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

	int i;

	SysTick_Config(SystemCoreClock / 10000);
	serial_init();

	/* LEDs */
	LPC_GPIO0->FIODIR |= (1 << 0);
	LPC_GPIO1->FIODIR |= (1 << 28);
	LPC_GPIO1->FIOSET = (1 << 28);
	LPC_GPIO1->FIODIR |= (1 << 29);

	__enable_irq();

	outputf("=== j4cDAC ===");

	outputf("skub_init()");
	skub_init();

	outputf("dac_init()");
	dac_init();

	outputf("lwip_init()");
	lwip_init();

	outputf("eth_init()");
	eth_init();

	outputf("sd_init()");
	sd_init();

	outputf("broadcast_init()");
	broadcast_init();

	outputf("ps_init()");
	ps_init();

	outputf("sink_init()");
	sink_init();

	outputf("USBInit()");
	USBInit();

	outputf("usbtest_init()");
	usbtest_init();

	outputf("bpm_init()");
	bpm_init();

	outputf("osc_init()");
	osc_init();

	outputf("Entering main loop...");

	ASSERT_EQUAL(dac_prepare(), 0);
	int status = 0;

	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		events_last[i] = events[i].start + time;
	}

	while (1) {
		tp_trianglewave_run();

		if (!(LPC_GPIO1->FIOPIN & (1 << 26))) {
			outputf("Blocking...");
			while (!(LPC_GPIO1->FIOPIN & (1 << 26)));
		}


		LPC_GPIO1->FIOCLR = (1 << 28);

		if (status) {
			LPC_GPIO0->FIOPIN = 1;
			LPC_GPIO1->FIOPIN = 0;
			status = 0;
		} else {
			LPC_GPIO0->FIOPIN = 0;
			LPC_GPIO1->FIOPIN = (1 << 29);
			status = 1;
		}

		LPC_GPIO1->FIOSET = (1 << 28);

		for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
			if (time > events_last[i] + events[i].period) {
				events[i].f();
				events_last[i] += events[i].period;
			}
		}

		eth_poll();

		bpm_check();
	}
}

