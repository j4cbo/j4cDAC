/* j4cDAC - panic!
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

#include <stdarg.h>
#include <stdio.h>
#include <hardware.h>

#ifndef PC_BUILD

#include "LPC17xx.h"

#include <attrib.h>
#include <serial.h>

#define DEBUG_UART      ((LPC_UART_TypeDef *)LPC_UART0)

#define PANIC_STRING	"***\r\n*** PANIC: "

void serial_send(const char *buf, int len);

void ATTR_VISIBLE panic_internal(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	hw_dac_init();

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer) - 2, fmt, va);

	if (n > (sizeof(buffer) - 2))
		n = sizeof(buffer) - 2;

	buffer[n] = '\r';
	buffer[n + 1] = '\n';

	serial_send(PANIC_STRING, sizeof(PANIC_STRING));
	serial_send(buffer, n + 2);

	hw_open_interlock_forever();
}

static inline void dump_stack(uint32_t * stack) {
	outputf("stack: %p", stack);
	outputf("r0 %08x r1 %08x r2 %08x r3 0x%08x",
		stack[0], stack[1], stack[2], stack[3]);
	outputf("r12 %08x lr %08x pc %08x psr 0x%08x",
		stack[4], stack[5], stack[6], stack[7]);
	int i;
	for (i = 8; i < 32; i++) {
		outputf("stack[%d]: %p", i, stack[i]);
	}
}

void HardFault_Handler_C(uint32_t * stack) ATTR_VISIBLE;
void HardFault_Handler_C(uint32_t * stack) {
	outputf("*** HARD FAULT ***");
	hw_dac_init();
	dump_stack(stack);
	hw_open_interlock_forever();
}

void BusFault_Handler_C(uint32_t * stack) ATTR_VISIBLE;
void BusFault_Handler_C(uint32_t * stack) {
	outputf("*** BUS FAULT ***");
	hw_dac_init();
	dump_stack(stack);
	hw_open_interlock_forever();
}

#else

#include <stdlib.h>

void panic(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer) - 2, fmt, va);

	if (n > (sizeof(buffer) - 2))
		n = sizeof(buffer) - 2;

	buffer[n] = '\r';
	buffer[n + 1] = '\n';

	puts(buffer);
	exit(1);
}

#endif
