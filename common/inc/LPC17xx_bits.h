#ifndef LPC17XX_BITS_H
#define LPC17XX_BITS_H

#define PCLK_CCLK4			0
#define PCLK_CCLK			1
#define PCLK_CCLK2			2
#define PCLK_CCLK8			3
#define PCLK_UART0(n)			((n) << 6)
#define PCLK_SSP1(n)			((n) << 20)

#define SCS_OSCRANGE			(1 << 4)
#define SCS_OSCEN			(1 << 5)
#define SCS_OSCSTAT			(1 << 6)

#define PLLnCON_Enable			(1 << 0)
#define PLLnCON_Connect			(1 << 1)

#define TnTCR_Counter_Enable		(1 << 0)
#define TnTCR_Counter_Reset		(1 << 1)

#define SSPnSR_Transmit_Empty		(1 << 0)
#define SSPnSR_Transmit_Not_Full	(1 << 1)

#define UARTnFCR_FIFO_Enable		(1 << 0)
#define UARTnFCR_RX_Reset		(1 << 1)
#define UARTnFCR_TX_Reset		(1 << 2)
#define UARTnTER_TX_Enable		(1 << 7)
#define UARTnLCR_8bit			(3 << 0)
#define UARTnLCR_2stop			(1 << 2)
#define UARTnLCR_DLAB			(1 << 7)
#define UARTnLSR_THR_Empty		(1 << 5)

#define WDMOD_WDEN			(1 << 0)
#define WDMOD_WDRESET			(1 << 1)
#define WDMOD_WDTOF			(1 << 2)
#define WDMOD_WDINT			(1 << 3)
#define WDSEL_WDLOCK			(1 << 31)

#define SSP_SR_TFE			(1 << 0)
#define SSP_SR_TNF			(1 << 1)
#define SSP_SR_RNE			(1 << 2)
#define SSP_SR_RFF			(1 << 3)
#define SSP_SR_BSY			(1 << 4)

#define SSP_CR0_DSS_8			(7)
#define SSP_CR0_DSS_16			(15)
#define SSP_CR1_SSP_EN			(1 << 1)

#define I2C_I2CONSET_AA			(1 << 2)
#define I2C_I2CONSET_SI			(1 << 3)
#define I2C_I2CONSET_STO		(1 << 4)
#define I2C_I2CONSET_STA		(1 << 5)
#define I2C_I2CONSET_I2EN		(1 << 6)
#define I2C_I2CONCLR_AAC		(1 << 2)
#define I2C_I2CONCLR_SIC		(1 << 3)
#define I2C_I2CONCLR_STAC		(1 << 5)
#define I2C_I2CONCLR_I2ENC		(1 << 6)

#define DMACC_Control_SBSIZE_1  (0 << 12)
#define DMACC_Control_DBSIZE_1  (0 << 15)
#define DMACC_Control_SBSIZE_4  (1 << 12)
#define DMACC_Control_DBSIZE_4  (1 << 15)
#define DMACC_Control_SBSIZE_8  (2 << 12)
#define DMACC_Control_DBSIZE_8  (2 << 15)
#define DMACC_Control_SWIDTH_8  (0 << 18)
#define DMACC_Control_DWIDTH_8  (0 << 21)
#define DMACC_Control_SI        (1 << 26)
#define DMACC_Control_DI        (1 << 27)

#define DMACC_Config_SrcPeripheral_SSP0Rx       (1 << 1)
#define DMACC_Config_SrcPeripheral_UART1Rx	(11 << 1)
#define DMACC_Config_DestPeripheral_SSP0Tx      (0 << 6)
#define DMACC_Config_DestPeripheral_UART1Tx	(10 << 6)
#define DMACC_Config_DestPeripheral_UART2Tx	(12 << 6)
#define DMACC_Config_DestPeripheral_UART3Tx	(14 << 6)
#define DMACC_Config_M2P        (1 << 11)
#define DMACC_Config_P2M        (2 << 11)
#define DMACC_Config_E          (1 << 0)
#define DMACC_Config_H          (1 << 18)

#endif
