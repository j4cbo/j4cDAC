/* j4cDAC DMX driver
 *
 * Copyright 2012 Jacob Potter
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

#include <dmx.h>
#include <tables.h>
#include <LPC17xx.h>
#include <LPC17xx_bits.h>
#include <stdint.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>
#include <param.h>

#define DMX_TIMER	LPC_TIM1
#define DMX_IRQHandler	TIMER1_IRQHandler
#define DMX_UART	LPC_UART1
#define DMX_DMA		LPC_GPDMACH2

uint8_t dmx_message[DMX_CHANNELS + 1] AHB0;

void dmx_init(void) {

	/* Configure UART TX pin */
	LPC_PINCON->PINSEL4 = (LPC_PINCON->PINSEL4 & ~(3 << 0)) | (2 << 0);

	/* Set up timer */
	DMX_TIMER->TCR = TnTCR_Counter_Enable | TnTCR_Counter_Reset;
	DMX_TIMER->PR = (SystemCoreClock / 250000 / 4) - 1;

	/* Start break, stop break, start data, reset */
	DMX_TIMER->MR0 = 0;
	DMX_TIMER->MR1 = 25;
	DMX_TIMER->MR2 = 28;
	DMX_TIMER->MR3 = 6249; /* 40 Hz */

	/* Interrupt on MR0, 1, 2; reset on MR3 match */
	DMX_TIMER->MCR = 02111;

	/* DMA for data */
	DMX_DMA->DMACCConfig = DMACC_Config_M2P
		| DMACC_Config_DestPeripheral_UART1Tx;
	DMX_DMA->DMACCLLI = 0;
	DMX_DMA->DMACCDestAddr = (uint32_t)&DMX_UART->THR;

	/* UART */
	DMX_UART->FCR = UARTnFCR_FIFO_Enable | UARTnFCR_RX_Reset \
		| UARTnFCR_TX_Reset;
	DMX_UART->FCR = 0;
	DMX_UART->IER = 0;
	DMX_UART->ACR = 0;
	DMX_UART->LCR |= UARTnLCR_DLAB;
	DMX_UART->DLM = 0;
	DMX_UART->DLL = 6;
	DMX_UART->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
	DMX_UART->FDR = 0x10;
	DMX_UART->FCR = UARTnFCR_FIFO_Enable;
	DMX_UART->TER |= UARTnTER_TX_Enable;

	NVIC_SetPriority(TIMER1_IRQn, 0);
	NVIC_EnableIRQ(TIMER1_IRQn);

	memset(dmx_message, 0, sizeof(dmx_message));

	/* Start! */
	DMX_TIMER->TCR = TnTCR_Counter_Enable;
}

void DMX_IRQHandler(void) {
	uint32_t ir = DMX_TIMER->IR;
	if (ir & 1) {
		/* Start break */
		DMX_UART->LCR |= (1 << 6);
		DMX_TIMER->IR = 1;
	} else if (ir & 2) {
		/* Stop break */
		DMX_UART->LCR &= ~(1 << 6);
		DMX_TIMER->IR = 2;
	} else {
		/* Start write */
		DMX_DMA->DMACCSrcAddr = (uint32_t)dmx_message;
		DMX_DMA->DMACCControl = (DMX_CHANNELS + 1) | DMACC_Control_SWIDTH_8 \
			| DMACC_Control_DWIDTH_8 | DMACC_Control_SI \
			| DMACC_Control_SBSIZE_8 | DMACC_Control_DBSIZE_8;
		DMX_DMA->DMACCConfig = DMACC_Config_M2P \
			| DMACC_Config_DestPeripheral_UART1Tx \
			| DMACC_Config_E;

		DMX_TIMER->IR = 4;
	}
}

void dmx_FPV_param(const char *path, int32_t v) {
	int index = atoi(path + 13);
	if (index < 1 || index > DMX_CHANNELS) return;
	dmx_message[index] = v;
}

TABLE_ITEMS(param_handler, dmx_osc_handlers,
	{ "/dmx/channel/*", PARAM_TYPE_I1, { .f1 = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
)

INITIALIZER(hardware, dmx_init);
