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
#include <skub.h>
#include <tables.h>

volatile uint32_t time;
volatile uint32_t mtime;

extern void tp_trianglewave_run(void);

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
	{ dhcp_coarse_tmr, 60000, "dhcp c", 35 },
	{ dhcp_fine_tmr, 500, "dhcp f", 25 },
	{ autoip_tmr, AUTOIP_TMR_INTERVAL, "autoip", 10 },
	{ broadcast_send, 1000, "broadcast", 10 }
};

int events_last[sizeof(events) / sizeof(events[0])];

TABLE(protocol);
TABLE(hardware);
TABLE(poll);

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

	outputf("=== j4cDAC ===");

	outputf("skub_init()");
	skub_init();

	outputf("lwip_init()");
	lwip_init();

	outputf("== hardware ==");

	for (i = 0; i < TABLE_LENGTH(hardware); i++) {
		outputf("%s()", hardware_table[i].name);
		hardware_table[i].f();
	}

	outputf("== protocol ==");

	for (i = 0; i < TABLE_LENGTH(protocol); i++) {
		outputf("%s()", protocol_table[i].name);
		protocol_table[i].f();
	}

	outputf("Entering main loop...");

	__enable_irq();

	int status = 0;

	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		events_last[i] = events[i].start + time;
	}

	while (1) {
//		tp_trianglewave_run();

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

		/* Check for periodic events */
		for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
			if (time > events_last[i] + events[i].period) {
				events[i].f();
				events_last[i] += events[i].period;
			}
		}

		/* Check the stuff we check on each loop iteration. */
		for (i = 0; i < TABLE_LENGTH(poll); i++) {
			poll_table[i].f();
		}
	}
}
