#ifndef LPC17XX_BITS_H
#define LPC17XX_BITS_H

#define PCLK_CCLK4			0
#define PCLK_CCLK			1
#define PCLK_CCLK2			2
#define PCLK_CCLK8			3
#define PCLK_UART0(n)			((n) << 6)

#define SCS_OSCRANGE			(1 << 4)
#define SCS_OSCEN			(1 << 5)
#define SCS_OSCSTAT			(1 << 6)

#define PLLnCON_Enable			(1 << 0)
#define PLLnCON_Connect			(1 << 1)

#define TnTCR_Counter_Enable		(1 << 0)
#define TnTCR_Counter_Reset		(1 << 1)

#define SSPnSR_Transmit_Not_Full	(1 << 1)

#define UARTnFCR_FIFO_Enable		(1 << 0)
#define UARTnFCR_RX_Reset		(1 << 1)
#define UARTnFCR_TX_Reset		(1 << 2)
#define UARTnTER_TX_Enable		(1 << 7)
#define UARTnLCR_8bit			(3 << 0)
#define UARTnLCR_DLAB			(1 << 7)
#define UARTnLSR_THR_Empty		(1 << 5)

#endif
