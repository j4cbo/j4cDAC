/* j4cDAC
 *
 * Copyright 2010, 2011 Jacob Potter
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
#include <serial.h>
#include "ether.h"
#include <lwip/init.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>
#include <ipv4/lwip/autoip.h>
#include <string.h>
#include <broadcast.h>
#include <assert.h>
#include <attrib.h>
#include <lightengine.h>
#include <skub.h>
#include <tables.h>
#include <playback.h>
#include <dac.h>
#include <hardware.h>
#include <file_player.h>
#include <dac_settings.h>

volatile uint32_t time;
volatile uint32_t mtime;
extern int f0ad_flag;
extern const char build[];

dac_settings_t settings;

void SysTick_Handler(void) {
	mtime++;
	if (mtime % 10 == 0) time++;
}

struct periodic_event {
	void (*f)(void);
	int period;
	const char *msg;
	int start;
} const events[] = {
	{ tcp_tmr, 250, "tcp", 100 },
	{ ip_reass_tmr, 1000, "ip_reass", 17 },
	{ etharp_tmr, 5000, "ip_reass", 177 },
	{ dhcp_coarse_tmr, 60000, "dhcp c", 35 },
	{ dhcp_fine_tmr, 500, "dhcp f", 25 },
	{ autoip_tmr, AUTOIP_TMR_INTERVAL, "autoip", 10 },
	{ broadcast_send, 1000, "broadcast", 10 },
	{ eth_check_link, 100, "eth_check_link", 2 }
};

int events_last[sizeof(events) / sizeof(events[0])];

TABLE(initializer_t, protocol);
TABLE(initializer_t, hardware);
TABLE(initializer_t, poll);

static void NOINLINE FPA_init() {
	int i;

	debugf("### Initializing Hardware ###\r\n");

	for (i = 0; i < TABLE_LENGTH(hardware); i++) {
		outputf("- %s()", hardware_table[i].name);
		hardware_table[i].f();
	}

	debugf("### Initializing Protocols ###\r\n");

	for (i = 0; i < TABLE_LENGTH(protocol); i++) {
		outputf("- %s()", protocol_table[i].name);
		protocol_table[i].f();
	}
}

void check_periodic_timers() {
	int i;
	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		if (time > events_last[i] + events[i].period) {
			events[i].f();
			events_last[i] += events[i].period;
			watchdog_feed();
		}
	}
}

int main(int argc, char **argv) __attribute__((noreturn));
int main(int argc, char **argv) {
	__disable_irq();

	LPC_SC->PCLKSEL0 = PCLKSEL0_INIT_VALUE;
	LPC_SC->PCLKSEL1 = PCLKSEL1_INIT_VALUE;
	LPC_SC->PCONP = PCONP_INIT_VALUE;
	clock_init();
	serial_init();

	/* Enable bus, usage, and mem faults. */
	SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk
		| SCB_SHCSR_USGFAULTENA_Msk;

#if 0
	/* Disable write buffers. This is useful for debugging - wild writes
	 * are imprecise exceptions unless the Cortex's write buffering is
	 * disabled. However, this has a significant performance hit, so it
	 * should only be set if necessary. */
	*((uint32_t *)0xE000E008) |= (1<<1);
#endif

	debugf("\r\n###############\r\n");
	debugf("# Ether Dream #\r\n");
	debugf("###############\r\n");
	debugf("Firmware: %s\r\n", build);

	debugf("RSID: %d\r\n", LPC_SC->RSID);
	LPC_SC->RSID = 0xF;

	hw_get_board_rev();

	debugf("Starting up: led");
	led_init();
	led_set_frontled(1);

	debugf(" skub");
	skub_init();

	debugf(" lwip\r\n");
	lwip_init();

	time = 0;
	SysTick_Config(SystemCoreClock / 10000);

	/* Initialize hardware */
	FPA_init();

	outputf("Entering main loop...");
	watchdog_init();

	/* Startup might have taken some time, so reset the periodic event
	 * timers. */
	int i;
	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		events_last[i] = events[i].start + time;
	}

	__enable_irq();

	playback_set_src(SRC_NETWORK);

	/* Default values */
	dac_set_rate(30000);
	ilda_set_fps_limit(30);

	while (1) {
		watchdog_feed();

		/* Check the stuff we check on each loop iteration. */
		for (i = 0; i < TABLE_LENGTH(poll); i++) {
			poll_table[i].f();
		}

		/* Check for periodic events */
		check_periodic_timers();

		if (f0ad_flag) {
			/* Re-enter the bootloader. */
			outputf("Reentering bootloader...");
			dac_stop(0);
			FORCE_BOOTLOAD_FLAG = FORCE_BOOTLOAD_VALUE;
			__disable_irq();

			/* The watchdog timer will kick us soon... */
			while(1);
		}
	}
}
