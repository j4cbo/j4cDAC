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

#ifndef PC_BUILD

#include "LPC17xx.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_pinsel.h"

#include <attrib.h>
#include <serial.h>

#define DEBUG_UART      ((LPC_UART_TypeDef *)LPC_UART0)

#define PANIC_STRING	"***\r\n*** PANIC: "

void ATTR_VISIBLE panic_internal(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer) - 2, fmt, va);

	if (n > (sizeof(buffer) - 2))
		n = sizeof(buffer) - 2;

	buffer[n] = '\r';
	buffer[n + 1] = '\n';

	UART_Send(DEBUG_UART, (uint8_t *) PANIC_STRING, sizeof(PANIC_STRING),
		  BLOCKING);
	UART_Send(DEBUG_UART, (uint8_t *) buffer, n + 2, BLOCKING);

	while (1);
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
