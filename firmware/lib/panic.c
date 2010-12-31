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

#include "LPC17xx.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_pinsel.h"

#include <stdarg.h>
#include <stdio.h>

#define DEBUG_UART      ((LPC_UART_TypeDef *)LPC_UART0)

#define PANIC_STRING	"***\r\n*** PANIC: "

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

	UART_Send(DEBUG_UART, (uint8_t *) PANIC_STRING, sizeof(PANIC_STRING),
		  BLOCKING);
	UART_Send(DEBUG_UART, (uint8_t *) buffer, n + 2, BLOCKING);

	while (1);
}
