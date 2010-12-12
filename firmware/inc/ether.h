/* LPC1758 Ethernet driver
 *
 * Jacob Potter
 * December 2010
 *
 * This file is based on "lpc17xx_emac.h", v2.0, 21 May 2010, by
 * the NXP MCU SW Application Team, which carries the following message:
 *
 **************************************************************************
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * products. This software is supplied "AS IS" without any warranties.
 * NXP Semiconductors assumes no responsibility or liability for the
 * use of the software, conveys no license or title under any patent,
 * copyright, or mask work right to the product. NXP Semiconductors
 * reserves the right to make changes in the software without
 * notification. NXP Semiconductors also make no representation or
 * warranty that such application will be suitable for the specified
 * use without further testing or modification.
 **********************************************************************/

#ifndef ETHER_H
#define ETHER_H

#include "LPC17xx.h"
#include "lpc_types.h"
#include <lwip/pbuf.h>

/* EMAC PHY status type definitions */
#define EMAC_PHY_STAT_LINK			(0)		/**< Link Status */
#define EMAC_PHY_STAT_SPEED			(1)		/**< Speed Status */
#define EMAC_PHY_STAT_DUP			(2)		/**< Duplex Status */

/* EMAC PHY device Speed definitions */
#define EMAC_MODE_AUTO				(0)		/**< Auto-negotiation mode */
#define EMAC_MODE_10M_FULL			(1)		/**< 10Mbps FullDuplex mode */
#define EMAC_MODE_10M_HALF			(2)		/**< 10Mbps HalfDuplex mode */
#define EMAC_MODE_100M_FULL			(3)		/**< 100Mbps FullDuplex mode */
#define EMAC_MODE_100M_HALF			(4)		/**< 100Mbps HalfDuplex mode */

/* EMAC Memory Buffer configuration for 16K Ethernet RAM */
#define EMAC_TX_FRAME_TOUT       0x00100000  /**< Frame Transmit timeout count      */

typedef struct {
	uint32_t	Mode;	
	uint8_t 	*pbEMAC_Addr;
} EMAC_CFG_Type;

/* Public Functions ----------------------------------------------------------- */
/** @defgroup EMAC_Public_Functions EMAC Public Functions
 * @{
 */
/* Init/DeInit EMAC peripheral */
int EMAC_Init(EMAC_CFG_Type *EMAC_ConfigStruct);
void EMAC_DeInit(void);

/* PHY functions --------------*/
int32_t EMAC_CheckPHYStatus(uint32_t ulPHYState);
int32_t EMAC_SetPHYMode(uint32_t ulPHYMode);
int32_t EMAC_UpdatePHYStatus(void);

/* Filter functions ----------*/
void EMAC_SetHashFilter(uint8_t dstMAC_addr[], FunctionalState NewState);
void EMAC_SetFilterMode(uint32_t ulFilterMode, FunctionalState NewState);

/* EMAC Interrupt functions -------*/
void EMAC_IntCmd(uint32_t ulIntType, FunctionalState NewState);
IntStatus EMAC_IntGetStatus(uint32_t ulIntType);

FlagStatus EMAC_CheckReceiveDataStatus(uint32_t ulRxStatType);
uint32_t EMAC_GetReceiveDataSize(void);
FlagStatus EMAC_GetWoLStatus(uint32_t ulWoLMode);

void eth_transmit(void * _info, struct pbuf * p);
void eth_init(void);

#endif
