#include "LPC17xx.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_pinsel.h"

#include <minilib.h>
#include <stdarg.h>

#define DEBUG_UART	((LPC_UART_TypeDef *)LPC_UART0)

void serial_init() {
	/* Configure pins */
	LPC_PINCON->PINSEL0 =
		(LPC_PINCON->PINSEL0 & ~((3 << 4) | (3 << 6)))
		| (1 << 4) | (1 << 6);

	/* Start up the UART */
	UART_CFG_Type UARTConfigStruct;
	UART_FIFO_CFG_Type UARTFIFOConfigStruct;
	UART_ConfigStructInit(&UARTConfigStruct);
	UARTConfigStruct.Baud_rate = 230400;
	UART_Init(DEBUG_UART, &UARTConfigStruct);
	UART_FIFOConfigStructInit(&UARTFIFOConfigStruct);
	UART_FIFOConfig(DEBUG_UART, &UARTFIFOConfigStruct);
	UART_TxCmd(DEBUG_UART, ENABLE);
}

void outputf(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer) - 2, fmt, va);

	if (n > (sizeof(buffer) - 2))
		n = sizeof(buffer) - 2;

	buffer[n] = '\r';
	buffer[n + 1] = '\n';

	UART_Send(DEBUG_UART, (uint8_t *) buffer, n + 2, BLOCKING);
}

void noutputf(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer), fmt, va);

	if (n > (sizeof(buffer)))
		n = sizeof(buffer);

	UART_Send(DEBUG_UART, (uint8_t *) buffer, n, BLOCKING);
}
static const char hexarr[] = "0123456789ABCDEF";

void hexdump(const char *data, int len) {
	int i;
	char c, buf[2];

	for (i = 0; i < len; i++) {
		c = data[i];
		buf[0] = hexarr[c >> 4];
		buf[1] = hexarr[c & 0xF];
		UART_Send(DEBUG_UART, (uint8_t *) buf, 2, BLOCKING);
	}
}

void serdump(const char *c, int len) {
	UART_Send(DEBUG_UART, (const uint8_t *)c, len, BLOCKING);
}
