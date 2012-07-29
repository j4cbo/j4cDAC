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
#include <osc.h>
#include <param.h>

#include <lwip/pbuf.h>
#include <lwip/udp.h>

#define DMX_TIMER		LPC_TIM1
#define DMX_TX_IRQHandler	TIMER1_IRQHandler
#define DMX_TX1_DMA		LPC_GPDMACH2
#define DMX_TX2_DMA		LPC_GPDMACH3
#define DMX_TX3_DMA		LPC_GPDMACH4

#define DMX_RX_UART		((LPC_UART_TypeDef *) LPC_UART1_BASE)

#define DMX_BAUD		250000

uint8_t dmx_universe_1[DMX_CHANNELS + 1] AHB0;
uint8_t dmx_universe_2[DMX_CHANNELS + 1] AHB0;
uint8_t dmx_universe_3[DMX_CHANNELS + 1];

static uint8_t dmx_tx_enabled_universes;

static struct {
	uint8_t input_buffer[DMX_CHANNELS + 4];
	enum {
		DMX_RX_IGNORE,
		DMX_RX_GOT_BREAK,
		DMX_RX_ACTIVE
	} state;
	int position;
	uint8_t unrecognized_start_code;
	uint8_t dirty;
} dmx_rx_state;

static uint8_t * const dmx_buffers[] = {
	0,
	dmx_universe_1,
	dmx_universe_2,
	dmx_universe_3,
};

static struct pbuf * dmx_in_pbuf;

static uint8_t osc_output_header[] = {
	'/', 'd', 'm', 'x', '1', 0, 0, 0,
	',', 'b', 0, 0, 0, 0, 2, 0
};

static struct ip_addr dmx_in_dest;
static uint16_t dmx_in_dest_port;

/* dmx_tx_enable_uart
 *
 * Enable the UART for a given DMX output.
 */
static void dmx_tx_enable_uart(LPC_UART_TypeDef *uart) {
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
}

void dmx_init(void) {
	int i;

	/* Configure UART1 TX and RX pins */
	LPC_PINCON->PINSEL4 = (LPC_PINCON->PINSEL4 & ~0xF) | 0xA;
	LPC_PINCON->PINSEL0 = (LPC_PINCON->PINSEL0 & ~0x300000) | 0x100000;
	LPC_PINCON->PINSEL1 |= (3 << 18);

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
	dmx_tx_enable_uart((LPC_UART_TypeDef *) LPC_UART1_BASE);
	dmx_tx_enable_uart((LPC_UART_TypeDef *) LPC_UART2_BASE);
	dmx_tx_enable_uart((LPC_UART_TypeDef *) LPC_UART3_BASE);
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
	memset(dmx_rx_state.input_buffer, 0, sizeof(dmx_rx_state.input_buffer));

	/* Start transmission! */
	DMX_TIMER->TCR = TnTCR_Counter_Enable;

	/* Receive */
	NVIC_SetPriority(UART1_IRQn, 0);
	NVIC_EnableIRQ(UART1_IRQn);
	DMX_RX_UART->IER = (1 << 2) | (1<<0);

	/* Set up pbuf for outgoing OSC packets */
	dmx_in_pbuf = pbuf_alloc(PBUF_TRANSPORT, 16, PBUF_ROM);
	dmx_in_pbuf->payload = (uint8_t *)osc_output_header;
	struct pbuf *data_pbuf = pbuf_alloc(PBUF_TRANSPORT, 512, PBUF_ROM);
	data_pbuf->payload = dmx_rx_state.input_buffer;
	pbuf_cat(dmx_in_pbuf, data_pbuf);
}

void UART1_IRQHandler(void) {
	LPC_UART_TypeDef *uart = DMX_RX_UART;

	uint32_t iir = uart->IIR;
	uint32_t lsr;
	uint32_t ch_old, ch;
	int pos;

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
		asm volatile("uxtb %0, %1" : "=r"(ch) : "r"(uart->RBR));

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
			pos = dmx_rx_state.position;

			ch_old = dmx_rx_state.input_buffer[pos];
			if (ch != ch_old)
				dmx_rx_state.dirty = 1;
			dmx_rx_state.input_buffer[pos] = ch;

			if (pos == DMX_CHANNELS - 1)
				dmx_rx_state.state = DMX_RX_IGNORE;

			dmx_rx_state.position = pos + 1;
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
		if (universes & (1 << 1)) {
			dmx_tx_enabled_universes |= (1 << 4);
			LPC_UART1->LCR = UARTnLCR_8bit | UARTnLCR_2stop | (1 << 6);
		}
		if (universes & (1 << 2)) {
			dmx_tx_enabled_universes |= (1 << 5);
			LPC_UART2->LCR = UARTnLCR_8bit | UARTnLCR_2stop | (1 << 6);
		}
		if (universes & (1 << 3)) {
			dmx_tx_enabled_universes |= (1 << 6);
			LPC_UART3->LCR = UARTnLCR_8bit | UARTnLCR_2stop | (1 << 6);
		}
		DMX_TIMER->IR = 1;
	} else if (ir & 2) {
		/* Stop break */
		if (universes & (1 << 4))
			LPC_UART1->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
		if (universes & (1 << 5))
			LPC_UART2->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
		if (universes & (1 << 6))
			LPC_UART3->LCR = UARTnLCR_8bit | UARTnLCR_2stop;
		DMX_TIMER->IR = 2;
	} else if (ir & 4) {
		/* Start write */
		if (universes & (1 << 4)) {
			DMX_TX1_DMA->DMACCSrcAddr = (uint32_t)dmx_universe_1;
			DMX_TX1_DMA->DMACCControl = DMX_DMACCControl_FLAGS;
			DMX_TX1_DMA->DMACCConfig =
				  DMACC_Config_DestPeripheral_UART1Tx
				| DMACC_Config_M2P | DMACC_Config_E;
		}
		if (universes & (1 << 5)) {
			DMX_TX2_DMA->DMACCSrcAddr = (uint32_t)dmx_universe_2;
			DMX_TX2_DMA->DMACCControl = DMX_DMACCControl_FLAGS;
			DMX_TX2_DMA->DMACCConfig =
				  DMACC_Config_DestPeripheral_UART2Tx
				| DMACC_Config_M2P | DMACC_Config_E;
		}
		if (universes & (1 << 6)) {
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

static void dmx_set_channel(unsigned universe, unsigned channel, int32_t v) {
	if (channel == 0) return;
	if (channel > DMX_CHANNELS) return;

	asm ("usat %0, 8, %1" : "=r"(v) : "r"(v));
	dmx_buffers[universe][channel] = v;
}

void dmx_set_channels(int universe, int base, int32_t *vs, int n) {
	if (universe == 0) return;
	if (universe > 3) return;

	int i;
	for (i = 0; i < n; i++)
		dmx_set_channel(universe, base + i, vs[i]);

	dmx_tx_enabled_universes |= (1 << universe);
}

static void dmx_FPV_param(const char *path, int32_t *vs, int n) {
	if (n < 1) return;
	dmx_set_channels(path[4] - '0', atoi(path + 6), vs, n);
}

static void dmx_indexed_FPV_param(const char *path, int32_t *vs, int n) {
	if (n < 2) return;
	dmx_set_channels(path[4] - '0', vs[0], vs + 1, n - 1);
}

static void dmx_blob_FPV_param(const char *path, uint8_t *blob, int n) {
        if (n > 512) return;
        int universe = path[4] - '0';
        if (universe == 0) return;
        if (universe > 3) return;

        memcpy(dmx_buffers[universe] + 1, blob, n);
	dmx_tx_enabled_universes |= (1 << universe);
}

static void dmx_input_FPV_param(const char *path, const char *ip, int32_t port) {
	outputf("DMX: %s %d", ip, port);
	if (port < 0 || port > 65535) {
		dmx_in_dest_port == 0;
		return;
	}
	if (!strcmp(ip, "me")) {
		dmx_in_dest = *osc_last_source;
		if (!port) dmx_in_dest_port = osc_last_port;
		else dmx_in_dest_port = port;
	} else if (inet_aton(ip, (struct in_addr *)&dmx_in_dest))
		dmx_in_dest_port = port;
	else
		dmx_in_dest_port = 0;
}

TABLE_ITEMS(param_handler, dmx_osc_handlers,
	{ "/dmx1/*", PARAM_TYPE_IN, { .fi = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx2/*", PARAM_TYPE_IN, { .fi = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx3/*", PARAM_TYPE_IN, { .fi = dmx_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx1", PARAM_TYPE_IN, { .fi = dmx_indexed_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx2", PARAM_TYPE_IN, { .fi = dmx_indexed_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx3", PARAM_TYPE_IN, { .fi = dmx_indexed_FPV_param }, PARAM_MODE_INT, 0, 255 },
	{ "/dmx1", PARAM_TYPE_BLOB, { .fb = dmx_blob_FPV_param } },
	{ "/dmx2", PARAM_TYPE_BLOB, { .fb = dmx_blob_FPV_param } },
	{ "/dmx3", PARAM_TYPE_BLOB, { .fb = dmx_blob_FPV_param } },
	{ "/dmx1/input", PARAM_TYPE_S1I1, { .fsi = dmx_input_FPV_param } },
)

void dmx_in_poll(void) {
	if (!dmx_in_dest_port)
		return;

	__disable_irq();
	if (!dmx_rx_state.dirty) {
		__enable_irq();
		return;
	}

	dmx_rx_state.dirty = 0;
	__enable_irq();

	udp_sendto(&osc_pcb, dmx_in_pbuf, &dmx_in_dest, dmx_in_dest_port);
}

INITIALIZER(hardware, dmx_init);
