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

#define DMX_TIMER		LPC_TIM1
#define DMX_TX_IRQHandler	TIMER1_IRQHandler
#define DMX_TX1_DMA		LPC_GPDMACH2
#define DMX_TX2_DMA		LPC_GPDMACH3
#define DMX_TX3_DMA		LPC_GPDMACH4

#define DMX_RX_UART		((LPC_UART_TypeDef *) LPC_UART1_BASE)

#define DMX_BAUD		250000

uint8_t dmx_universe_1[DMX_CHANNELS + 1] AHB0;
uint8_t dmx_universe_2[DMX_CHANNELS + 1];
uint8_t dmx_universe_3[DMX_CHANNELS + 1];
uint8_t dmx_input_buffer[DMX_CHANNELS] AHB0;

uint8_t dmx_tx_enabled_universes;

struct {
	enum {
		DMX_RX_IGNORE,
		DMX_RX_GOT_BREAK,
		DMX_RX_ACTIVE
	} state;
	uint8_t unrecognized_start_code;
	int position;
} dmx_rx_state;

static LPC_UART_TypeDef * const dmx_uarts[] = {
	0,	// Channel 0 is bogus
	((LPC_UART_TypeDef *) LPC_UART1_BASE),
	((LPC_UART_TypeDef *) LPC_UART2_BASE),
	((LPC_UART_TypeDef *) LPC_UART3_BASE),
};

static uint8_t * const dmx_buffers[] = {
	0,	// Channel 0 is bogus
	dmx_universe_1,
	dmx_universe_2,
	dmx_universe_3,
};

/* dmx_enable_universe
 *
 * Enable the UART for a given DMX output and set flags to start
 * transmitting. Returns 0 on success, -1 if universe is invalid.
 */
int dmx_enable_universe(int universe) {
	if (universe < 1 || universe > 3)
		return -1;

	if (dmx_tx_enabled_universes & (1 << universe))
		return 0;

	LPC_UART_TypeDef *uart = dmx_uarts[universe];

	uart->FCR = UARTnFCR_FIFO_Enable | UARTnFCR_RX_Reset \
		| UARTnFCR_TX_Reset;
	uart->FCR = 0;
	uart->IER = 0;
	uart->ACR = 0;
	uart->LCR |= UARTnLCR_DLAB;
	uart->DLM = 0;
	uart->DLL = SystemCoreClock / (DMX_BAUD * 16 * 4);
	uart->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
	uart->FDR = 0x10;
	uart->FCR = UARTnFCR_FIFO_Enable;
	uart->TER |= UARTnTER_TX_Enable;

	dmx_tx_enabled_universes |= (1 << universe);
	return 0;
}

void dmx_init(void) {
	int i;

	/* Configure UART1 TX and RX pins */
	LPC_PINCON->PINSEL4 = (LPC_PINCON->PINSEL4 & ~0xF) | 0xA;

	/* Set up timer */
	DMX_TIMER->TCR = TnTCR_Counter_Enable | TnTCR_Counter_Reset;
	DMX_TIMER->PR = (SystemCoreClock / DMX_BAUD / 4) - 1;

	/* Start break, stop break, start data, reset */
	DMX_TIMER->MR0 = 0;
	DMX_TIMER->MR1 = 25;
	DMX_TIMER->MR2 = 40;
	DMX_TIMER->MR3 = 6249; /* 40 Hz */

	/* Interrupt on MR0, 1, 2; reset on MR3 match */
	DMX_TIMER->MCR = 02111;

	/* DMA for data */
	DMX_TX1_DMA->DMACCConfig = DMACC_Config_M2P
		| DMACC_Config_DestPeripheral_UART1Tx;
	DMX_TX1_DMA->DMACCLLI = 0;
	DMX_TX1_DMA->DMACCDestAddr = (uint32_t)&LPC_UART1->THR;
	DMX_TX2_DMA->DMACCConfig = DMACC_Config_M2P
		| DMACC_Config_DestPeripheral_UART2Tx;
	DMX_TX2_DMA->DMACCLLI = 0;
	DMX_TX2_DMA->DMACCDestAddr = (uint32_t)&LPC_UART2->THR;
	DMX_TX3_DMA->DMACCConfig = DMACC_Config_M2P
		| DMACC_Config_DestPeripheral_UART3Tx;
	DMX_TX3_DMA->DMACCLLI = 0;
	DMX_TX3_DMA->DMACCDestAddr = (uint32_t)&LPC_UART3->THR;

	NVIC_SetPriority(TIMER1_IRQn, 0);
	NVIC_EnableIRQ(TIMER1_IRQn);

	memset(dmx_universe_1, 0, sizeof(dmx_universe_1));
	memset(dmx_universe_2, 0, sizeof(dmx_universe_2));
	memset(dmx_universe_3, 0, sizeof(dmx_universe_3));
	memset(dmx_input_buffer, 0, sizeof(dmx_input_buffer));

	/* Start transmission! */
	DMX_TIMER->TCR = TnTCR_Counter_Enable;

	/* Receive */
	NVIC_SetPriority(UART1_IRQn, 0);
	NVIC_EnableIRQ(UART1_IRQn);
	DMX_RX_UART->IER = (1 << 2) | (1<<0);
}

void UART1_IRQHandler(void) {
	LPC_UART_TypeDef *uart = DMX_RX_UART;

	uint32_t iir = uart->IIR;
	uint32_t lsr;
	uint8_t ch;

	switch (iir & 0xF) {
	case 1:
		/* Spurious interrut */
		return;

	case 0x6:
		/* Break received */
		lsr = uart->LSR;
		ch = uart->RBR;
		if (lsr & (1 << 4)) {
			dmx_rx_state.state = DMX_RX_GOT_BREAK;
		}

		break;
	case 0x4:
		/* RX */
		lsr = uart->LSR;
		ch = uart->RBR;

		switch (dmx_rx_state.state) {
		case DMX_RX_IGNORE:
			return;

		case DMX_RX_GOT_BREAK:
			/* Is this a data packet? */
			if (ch != 0) {
				dmx_rx_state.unrecognized_start_code = ch;
				dmx_rx_state.state = DMX_RX_IGNORE;
			} else {
				dmx_rx_state.state = DMX_RX_ACTIVE;
				dmx_rx_state.position = 0;
			}

			return;

		default:
			dmx_input_buffer[dmx_rx_state.position++] = ch;
			if (dmx_rx_state.position == DMX_CHANNELS)
				dmx_rx_state.state = DMX_RX_IGNORE;

			return;
		}

	case 0x2:
		/* THR empty - ignore */
		return;

	default:
		emergency_exit_1(iir, { 
			panic("DMX IRQ: bogus interrupt %x", iir >> 1);
		});
		break;
	}
}

#define DMX_DMACCControl_FLAGS \
	(DMX_CHANNELS + 1) | DMACC_Control_SWIDTH_8 | DMACC_Control_DWIDTH_8 \
	| DMACC_Control_SI | DMACC_Control_SBSIZE_1 | DMACC_Control_DBSIZE_1;

void DMX_TX_IRQHandler(void) {
	uint32_t ir = DMX_TIMER->IR;
	uint8_t universes = dmx_tx_enabled_universes;
	if (ir & 1) {
		/* Start break */
		if (universes & (1 << 1))
			LPC_UART1->LCR = UARTnLCR_8bit | UARTnLCR_2stop | (1 << 6);
		if (universes & (1 << 2))
			LPC_UART2->LCR = UARTnLCR_8bit | UARTnLCR_2stop | (1 << 6);
		if (universes & (1 << 3))
			LPC_UART3->LCR = UARTnLCR_8bit | UARTnLCR_2stop | (1 << 6);
		DMX_TIMER->IR = 1;
	} else if (ir & 2) {
		/* Stop break */
		if (universes & (1 << 1))
			LPC_UART1->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
		if (universes & (1 << 2))
			LPC_UART2->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
		if (universes & (1 << 3))
			LPC_UART3->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
		DMX_TIMER->IR = 2;
	} else if (ir & 4) {
		/* Start write */
		if (universes & (1 << 1)) {
			DMX_TX1_DMA->DMACCSrcAddr = (uint32_t)dmx_universe_1;
			DMX_TX1_DMA->DMACCControl = DMX_DMACCControl_FLAGS;
			DMX_TX1_DMA->DMACCConfig =
				  DMACC_Config_DestPeripheral_UART1Tx
				| DMACC_Config_M2P | DMACC_Config_E;
		}
		if (universes & (1 << 2)) {
			DMX_TX2_DMA->DMACCSrcAddr = (uint32_t)dmx_universe_2;
			DMX_TX2_DMA->DMACCControl = DMX_DMACCControl_FLAGS;
			DMX_TX2_DMA->DMACCConfig =
				  DMACC_Config_DestPeripheral_UART2Tx
				| DMACC_Config_M2P | DMACC_Config_E;
		}
		if (universes & (1 << 3)) {
			DMX_TX3_DMA->DMACCSrcAddr = (uint32_t)dmx_universe_3;
			DMX_TX3_DMA->DMACCControl = DMX_DMACCControl_FLAGS;
			DMX_TX3_DMA->DMACCConfig =
				  DMACC_Config_DestPeripheral_UART3Tx
				| DMACC_Config_M2P | DMACC_Config_E;
		}
		DMX_TIMER->IR = 4;
	} else if (ir != 0) {
		emergency_exit_1(ir, {
			panic("DMX TX: unexpected IR 0x%08x", ir);
		});
	}
}

void dmx_set_channel(int universe, int channel, int32_t v) {
	if (channel < 1 || channel > DMX_CHANNELS) return;
	if (v < 0) v = 0;
	if (v > 255) v = 255;

	if (dmx_enable_universe(universe) < 0)
		return;

	dmx_buffers[universe][channel] = v;
}

void dmx_FPV_param(const char *path, int32_t v) {
	dmx_set_channel(path[4] - '0', atoi(path + 14), v);
}

void dmx_multi_FPV_param(const char *path, int32_t *vs, int n) {
	if (n < 2) return;
	int universe = path[4] - '0';
	int i;
	if (dmx_enable_universe(universe) < 0)
		return;
	int32_t base_channel = vs[0];
	for (i = 0; i < n-1; i++) {
		dmx_set_channel(universe, base_channel + i, vs[i + 1]);
	}
}

TABLE_ITEMS(param_handler, dmx_osc_handlers,
	{ "/dmx1/channel/*", PARAM_TYPE_I1, { .f1 = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx2/channel/*", PARAM_TYPE_I1, { .f1 = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx3/channel/*", PARAM_TYPE_I1, { .f1 = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx1", PARAM_TYPE_IN, { .fi = dmx_multi_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx2", PARAM_TYPE_IN, { .fi = dmx_multi_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx3", PARAM_TYPE_IN, { .fi = dmx_multi_FPV_param }, PARAM_MODE_INT, 0, 255 },
)

INITIALIZER(hardware, dmx_init);
