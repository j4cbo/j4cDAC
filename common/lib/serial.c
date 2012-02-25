#include "LPC17xx.h"
#include "LPC17xx_bits.h"
#include <stdarg.h>
#include <string.h>

#define DEBUG_UART	LPC_UART0

int vsnprintf(char *buf, unsigned int len, const char *fmt, va_list va);

void serial_init() {
	/* Configure pins */
	LPC_PINCON->PINSEL0 =
		(LPC_PINCON->PINSEL0 & ~((3 << 4) | (3 << 6)))
		| (1 << 4) | (1 << 6);

	/* Reset FIFOs */
	DEBUG_UART->FCR = UARTnFCR_FIFO_Enable | UARTnFCR_RX_Reset \
	                | UARTnFCR_TX_Reset;
	DEBUG_UART->FCR = 0;
	DEBUG_UART->IER = 0;
	DEBUG_UART->ACR = 0;

	uint32_t inputclock = SystemCoreClock >> 4;
	uint32_t baudrate = 230400;

	/* Calculate baud rate - there's a fractional baud rate generator
	 * in the LPC17xx UART, so we brute-force search for the best setting
	 * for it. */
	int div, mul;
	int bestError = baudrate, bestDiv = 0, bestMul = 0, bestDivisor = 0;
	for (mul = 1; mul < 16; mul++) {
		for (div = 0; div < mul; div++) {
			int frac = (inputclock * mul) / (mul + div);
			int divisor = frac / baudrate;
			if ((frac * baudrate) / (baudrate / 2))
				divisor++;
			if (divisor >= 65536)
				continue;
			if (divisor < 1)
				continue;

			int resultbaud = frac / divisor;
			int error = resultbaud - baudrate;
			if (error < 0)
				error = -error;

			if (error < bestError) {
				bestError = error;
				bestMul = mul;
				bestDiv = div;
				bestDivisor = divisor;
			}

			if (!error)
				break;
		}
	}

	DEBUG_UART->LCR |= UARTnLCR_DLAB;
	DEBUG_UART->DLM = (bestDivisor >> 8) & 0xff;
	DEBUG_UART->DLL = bestDivisor & 0xff;
	DEBUG_UART->LCR &= ~UARTnLCR_DLAB;
	DEBUG_UART->FDR = (bestMul << 4) | bestDiv;
	DEBUG_UART->LCR = UARTnLCR_8bit;
	DEBUG_UART->FCR = UARTnFCR_FIFO_Enable;
	DEBUG_UART->TER |= UARTnTER_TX_Enable;
}

void serial_send(const char *buf, int len) {
	while (len--) {
		while (!(DEBUG_UART->LSR & UARTnLSR_THR_Empty));
		DEBUG_UART->THR = ((uint8_t)*buf);
		buf++;
	}
}

void outputf(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer) - 2, fmt, va);

	if (n > (sizeof(buffer) - 2))
		n = sizeof(buffer) - 2;

	if (n > 1 && !buffer[n-1] && buffer[n-2] == '\n')
		n -= 2;

	buffer[n] = '\r';
	buffer[n + 1] = '\n';

	serial_send(buffer, n + 2);
}

void debugf(const char *fmt, ...) {
	va_list va;
	char buffer[80];
	int n;

	va_start(va, fmt);
	n = vsnprintf(buffer, sizeof(buffer), fmt, va);

	if (n > (sizeof(buffer)))
		n = sizeof(buffer);

	serial_send(buffer, n);
}

static const char hexarr[] = "0123456789ABCDEF";

void hexdump(const char *data, int len) {
	int i;
	char c, buf[2];

	for (i = 0; i < len; i++) {
		c = data[i];
		buf[0] = hexarr[c >> 4];
		buf[1] = hexarr[c & 0xF];
		serial_send(buf, 2);
	}
}
