/* LPC1758 Ethernet driver
 *
 * Jacob Potter
 * December 2010
 *
 * This file is based on "lpc17xx_emac.c", v2.0, 21 May 2010, by
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

#include "lpc17xx_clkpwr.h"
#include "serial.h"
#include "dp83848.h"
#include "mdio.h"
#include <string.h>

#include <ether.h>
#include <ether_private.h>
#include <attrib.h>

#define NUM_TX_DESC	(TCP_SND_QUEUELEN + 2)
#define NUM_RX_BUF	12
#define RX_BUF_SIZE	1520

extern uint8_t mac_address[6];

/* Transmit descriptors, receive descriptors, and receive buffers */
static struct pbuf *eth_tx_pbufs[NUM_TX_DESC];
static TX_Desc eth_tx_desc[NUM_TX_DESC] AHB0;
static TX_Stat eth_tx_stat[NUM_TX_DESC] AHB0;
static int eth_tx_free_consume = 0;

static struct pbuf *eth_rx_pbufs[NUM_RX_BUF] AHB0;
static RX_Desc eth_rx_desc[NUM_RX_BUF] AHB0;
static RX_Stat eth_rx_stat[NUM_RX_BUF] AHB0 __attribute__((aligned (8)));
static int eth_rx_read_index = 0;

void handle_packet(struct pbuf *p);

/* Private Functions ---------------------------------------------------------- */

/* void eth_init_descriptors(void)
 *
 * Set up the transmit and receive descriptors. Tx generally lives in main
 * SRAM; Rx lives in AHB SRAM and is statically allocated, so we set up the
 * pointers once during initialization.
 */
static int COLD eth_init_descriptors() {
	uint32_t i;
	struct pbuf *p;

	for (i = 0; i < NUM_RX_BUF; i++) {
		p = pbuf_alloc(PBUF_RAW, RX_BUF_SIZE, PBUF_POOL);

		if (!p) {
			outputf("MAC: FATAL: out of memory during Rx init");
			return -1;
		}

		eth_rx_desc[i].Packet = (uint32_t)p->payload;
		eth_rx_desc[i].Ctrl = p->len - 1;
		eth_rx_pbufs[i] = p;
	}

	memset(&eth_tx_desc, 0, sizeof(eth_tx_desc));
	memset(&eth_tx_stat, 0, sizeof(eth_tx_stat));
	memset(&eth_rx_stat, 0, sizeof(eth_rx_stat));

	/* Point the hardware at our descriptors. */
	LPC_EMAC->TxDescriptor = (uint32_t) (&eth_tx_desc[0]);
	LPC_EMAC->TxStatus = (uint32_t) (&eth_tx_stat[0]);
	LPC_EMAC->TxDescriptorNumber = NUM_TX_DESC - 1;
	LPC_EMAC->RxDescriptor = (uint32_t) (&eth_rx_desc[0]);
	LPC_EMAC->RxStatus = (uint32_t) (&eth_rx_stat[0]);
	LPC_EMAC->RxDescriptorNumber = NUM_RX_BUF - 1;

	/* Reset produce and consume indices */
	LPC_EMAC->TxProduceIndex = 0;
	LPC_EMAC->RxConsumeIndex = 0;

	return 0;
}

/* void eth_set_mac(uint8_t *addr)
 *
 * Set the MAC address of the system.
 */
static void COLD eth_set_mac(uint8_t * addr) {
	LPC_EMAC->SA0 = ((uint32_t) addr[5] << 8) | (uint32_t) addr[4];
	LPC_EMAC->SA1 = ((uint32_t) addr[3] << 8) | (uint32_t) addr[2];
	LPC_EMAC->SA2 = ((uint32_t) addr[1] << 8) | (uint32_t) addr[0];
}

/* End of Private Functions --------------------------------------------------- */

/* Public Functions ----------------------------------------------------------- */
/** @addtogroup EMAC_Public_Functions
 * @{
 */

/*********************************************************************//**
 * @brief		Initializes the EMAC peripheral according to the specified
*               parameters in the EMAC_ConfigStruct.
 * @param[in]	EMAC_ConfigStruct Pointer to a EMAC_CFG_Type structure
*                    that contains the configuration information for the
*                    specified EMAC peripheral.
 * @return		None
 *
 * Note: This function will initialize EMAC module according to procedure below:
 *  - Remove the soft reset condition from the MAC
 *  - Configure the PHY via the MIIM interface of the MAC
 *  - Select RMII mode
 *  - Configure the transmit and receive DMA engines, including the descriptor arrays
 *  - Configure the host registers (MAC1,MAC2 etc.) in the MAC
 *  - Enable the receive and transmit data paths
 *  In default state after initializing, only Rx Done and Tx Done interrupt are enabled,
 *  all remain interrupts are disabled
 *  (Ref. from LPC17xx UM)
 **********************************************************************/

int COLD EMAC_Init(void) {
	/* Initialize the EMAC Ethernet controller. */
	int32_t regv, tout;

	/* Set up clock and power for Ethernet module */
	CLKPWR_ConfigPPWR (CLKPWR_PCONP_PCENET, ENABLE);

	/* Reset all EMAC internal modules */
	LPC_EMAC->MAC1 = EMAC_MAC1_RES_TX | EMAC_MAC1_RES_MCS_TX | EMAC_MAC1_RES_RX |
	                 EMAC_MAC1_RES_MCS_RX | EMAC_MAC1_SIM_RES | EMAC_MAC1_SOFT_RES;

	LPC_EMAC->Command = EMAC_CR_REG_RES | EMAC_CR_TX_RES | EMAC_CR_RX_RES |
	                    EMAC_CR_PASS_RUNT_FRM;

	/* Initialize MAC control registers. */
	LPC_EMAC->MAC1 = EMAC_MAC1_PASS_ALL;
	LPC_EMAC->MAC2 = EMAC_MAC2_CRC_EN | EMAC_MAC2_PAD_EN;
	LPC_EMAC->MAXF = RX_BUF_SIZE;

	/* Set up Ethernet parameters */
	LPC_EMAC->CLRT = EMAC_CLRT_DEF;
	LPC_EMAC->IPGR = EMAC_IPGR_P2_DEF;

	/* Enable Reduced MII interface. */
	LPC_EMAC->Command = EMAC_CR_RMII | EMAC_CR_PASS_RUNT_FRM;

	/* Reset Reduced MII Logic. */
	LPC_EMAC->SUPP = EMAC_SUPP_RES_RMII;
	LPC_EMAC->SUPP = 0;

	/* Put the DP83848C in reset mode */
	mdio_write(EMAC_PHY_REG_BMCR, EMAC_PHY_BMCR_RESET);

	debugf("  - PHY: ");

	/* Wait for hardware reset to end. */
	for (tout = EMAC_PHY_RESP_TOUT; tout; tout--) {
		regv = mdio_read(EMAC_PHY_REG_BMCR);
		if (!(regv & (EMAC_PHY_BMCR_RESET | EMAC_PHY_BMCR_POWERDOWN))) {
			/* Reset complete, device not Power Down. */
			break;
		}
		if (tout == 0){
			// Time out, return ERROR
			debugf("timed out\r\n");
			return (ERROR);
		}
	}

	/* Put the PHY in RMII mode */
	mdio_write(DP83848_RBR, 0x21);

	// Set PHY mode
	if (EMAC_SetPHYMode(EMAC_MODE_AUTO) < 0) {
		return (ERROR);
	}

	// Set EMAC address
	eth_set_mac(mac_address);

	/* Initialize Tx and Rx DMA Descriptors */
	if (eth_init_descriptors() < 0) return ERROR;

	return SUCCESS;
}

/*********************************************************************//**
 * @brief		De-initializes the EMAC peripheral registers to their
*                  default reset values.
 * @param[in]	None
 * @return 		None
 **********************************************************************/
void EMAC_DeInit(void)
{
	// Disable all interrupt
	LPC_EMAC->IntEnable = 0x00;
	// Clear all pending interrupt
	LPC_EMAC->IntClear = (0xFF) | (EMAC_INT_SOFT_INT | EMAC_INT_WAKEUP);

	/* TurnOff clock and power for Ethernet module */
	CLKPWR_ConfigPPWR (CLKPWR_PCONP_PCENET, DISABLE);
}

/*********************************************************************//**
 * @brief		Set specified PHY mode in EMAC peripheral
 * @param[in]	ulPHYMode	Specified PHY mode, should be:
 * 							- EMAC_MODE_AUTO
 * 							- EMAC_MODE_10M_FULL
 * 							- EMAC_MODE_10M_HALF
 * 							- EMAC_MODE_100M_FULL
 * 							- EMAC_MODE_100M_HALF
 * @return		Return (0) if no error, otherwise return (-1)
 **********************************************************************/
int32_t COLD EMAC_SetPHYMode(uint32_t ulPHYMode)
{
	int32_t id1, id2;

	/* Check if this is a DP83848C PHY. */
	id1 = mdio_read(EMAC_PHY_REG_IDR1);
	id2 = mdio_read(EMAC_PHY_REG_IDR2);

	outputf("%08x %08x", id1, id2);

	if (((id1 << 16) | (id2 & 0xFFF0)) == EMAC_DP83848C_ID) {
		switch(ulPHYMode){
		case EMAC_MODE_AUTO:
			mdio_write(EMAC_PHY_REG_BMCR, EMAC_PHY_AUTO_NEG);
			break;
		case EMAC_MODE_10M_FULL:
			/* Connect at 10MBit full-duplex */
			mdio_write(EMAC_PHY_REG_BMCR, EMAC_PHY_FULLD_10M);
			break;
		case EMAC_MODE_10M_HALF:
			/* Connect at 10MBit half-duplex */
			mdio_write(EMAC_PHY_REG_BMCR, EMAC_PHY_HALFD_10M);
			break;
		case EMAC_MODE_100M_FULL:
			/* Connect at 100MBit full-duplex */
			mdio_write(EMAC_PHY_REG_BMCR, EMAC_PHY_FULLD_100M);
			break;
		case EMAC_MODE_100M_HALF:
			/* Connect at 100MBit half-duplex */
			mdio_write(EMAC_PHY_REG_BMCR, EMAC_PHY_HALFD_100M);
			break;
		default:
			// un-supported
			return (-1);
		}
	}
	// It's not correct module ID
	else {
		outputf("bad module id");
		return -1;
	}

	EMAC_UpdatePHYStatus();

	// Complete
	return 0;
}


/*********************************************************************//**
 * @brief		Auto-Configures value for the EMAC configuration register to
 * 				match with current PHY mode
 * @param[in]	None
 * @return		Return (0) if no error, otherwise return (-1)
 *
 * Note: The EMAC configuration will be auto-configured:
 * 		- Speed mode.
 * 		- Half/Full duplex mode
 **********************************************************************/
int32_t EMAC_UpdatePHYStatus(void)
{
	int32_t regv;

	/* Check the link status. */
	regv = mdio_read(EMAC_PHY_REG_STS);
	if (regv == 0xffff) return -1;
	if (!(regv & EMAC_PHY_SR_LINK)) {
		return -1;
	}
	if (!(regv & EMAC_PHY_SR_AUTO_DONE)) {
		return -1;
	}

	/* Configure Full/Half Duplex mode. */
	if (regv & EMAC_PHY_SR_DUP) {
	/* Full duplex is enabled. */
			LPC_EMAC->MAC2    |= EMAC_MAC2_FULL_DUP;
			LPC_EMAC->Command |= EMAC_CR_FULL_DUP;
			LPC_EMAC->IPGT     = EMAC_IPGT_FULL_DUP;
	} else {
		/* Half duplex mode. */
		LPC_EMAC->IPGT = EMAC_IPGT_HALF_DUP;
	}
	if (regv & EMAC_PHY_SR_SPEED) {
		/* 10MBit mode. */
		LPC_EMAC->SUPP = 0;
	} else {
		/* 100MBit mode. */
		LPC_EMAC->SUPP = EMAC_SUPP_SPEED;
	}

	// Set Receive Filter register: enable broadcast and multicast
	LPC_EMAC->RxFilterCtrl = EMAC_RFC_MCAST_EN | EMAC_RFC_BCAST_EN | EMAC_RFC_PERFECT_EN;

	/* Enable Rx Done and Tx Done interrupt for EMAC */
	LPC_EMAC->IntEnable = EMAC_INT_RX_DONE | EMAC_INT_TX_DONE;

	/* Reset all interrupts */
	LPC_EMAC->IntClear  = 0xFFFF;

	/* Enable receive and transmit mode of MAC Ethernet core */
	LPC_EMAC->Command  |= (EMAC_CR_RX_EN | EMAC_CR_TX_EN);
	LPC_EMAC->MAC1     |= EMAC_MAC1_REC_EN;

	// Complete
	return 0;
}

#include <serial.h>

#include <lwip/init.h>
#include <netif/etharp.h>

static const uint8_t ether_pins[] = { 0, 1, 4, 8, 9, 10, 14, 15 };

/* Clean up old transmitted pbufs */
void eth_tx_cleanup() {
	int consume = LPC_EMAC->TxConsumeIndex;
	int last = eth_tx_free_consume;

	while (last != consume) {
#ifdef ETHER_SPEW
		outputf("reclaiming pbuf %d: status 0x%08x", last, eth_tx_stat[last]);
#endif
		pbuf_free(eth_tx_pbufs[last]);
		eth_tx_pbufs[last] = 0;
		last++;
		if (last == NUM_TX_DESC) last = 0;
	}

	eth_tx_free_consume = last;
}

static int eth_capacity() {
	int produce = LPC_EMAC->TxProduceIndex;
	int consume = LPC_EMAC->TxConsumeIndex;

#ifdef ETHER_SPEW
	outputf("MAC: capacity: prod %d, cons %d", produce, consume);
#endif

	int capacity = (consume - produce) - 1;
	if (capacity < 0)
		capacity += NUM_TX_DESC;

	return capacity;
}

err_t eth_transmit_FPV_netif_linkoutput(struct netif * _info, struct pbuf * p) {

	/* Find the number of fragments in this pbuf */
	int len = 0, n = 0;
	struct pbuf * ptr;
	for (ptr = p; ptr; ptr = ptr->next) {
		len += ptr->len;
		n++;
	}

	if (n > (NUM_TX_DESC - 1)) {
		outputf("MAC: FATAL: pbuf too long: %d > %d",
		         n, NUM_TX_DESC - 1);
		pbuf_free(p);
		return ERR_ABRT;
	}

	if (eth_capacity() < n) {
		int i = 0;

		outputf("MAC: tx buf full, waiting for space...");
		do {
			i++;
		} while (eth_capacity() < n);
		outputf("MAC: took %d iters", i);
	}

	int produce = LPC_EMAC->TxProduceIndex;

	/* "Sup, hardware" */
	for (; p; p = p->next) {
		eth_tx_desc[produce].Packet = (uint32_t) p->payload;
		eth_tx_desc[produce].Ctrl = (p->len - 1) | (p->next ? 0 : EMAC_TCTRL_LAST) | EMAC_TCTRL_INT;

		pbuf_ref(p);
		eth_tx_pbufs[produce] = p;

		produce = (produce + 1) % NUM_TX_DESC;
	}

	/* Shazam. */
	LPC_EMAC->TxProduceIndex = produce;
	return ERR_OK;
}

void eth_poll_1(void) {

	/* Clean up old Tx records */
	eth_tx_cleanup();

	int produce = LPC_EMAC->RxProduceIndex;
	int readpos = eth_rx_read_index;
	struct pbuf * p;

	int packets_waiting = produce - readpos;
	if (packets_waiting < 0) packets_waiting += NUM_RX_BUF;

	if (!packets_waiting) return; 

#ifdef ETHER_SPEW
	outputf("p: %d %d/%d/%d", packets_waiting, consume, readpos, produce);
#endif

	uint32_t status = eth_rx_stat[readpos].Info;
	p = eth_rx_pbufs[readpos];

	int length = (status & EMAC_RINFO_SIZE) + 1;
	pbuf_realloc(p, length);

#ifdef ETHER_SPEW
	outputf("Rx %d: fl %08x, pbuf %08x, d %p/%d",  consume, status, p, p->payload, length);
#endif

	eth_rx_read_index = (readpos + 1) % NUM_RX_BUF;
	handle_packet(p);
}

void eth_poll_2(void) {
	struct pbuf *p;
	int readpos = eth_rx_read_index;
	int consume = LPC_EMAC->RxConsumeIndex;

	while (consume != readpos) {
		/* Try to allocate a new pbuf */
		p = pbuf_alloc(PBUF_RAW, RX_BUF_SIZE, PBUF_POOL);
		if (!p) {
			outputf("MAC: out of memory refilling Rx: cons/ri/prod %d/%d/%d",
			        consume, readpos, LPC_EMAC->RxProduceIndex);
			break;
		}

		eth_rx_desc[consume].Packet = (uint32_t)p->payload;
		eth_rx_desc[consume].Ctrl = p->len - 1;
		eth_rx_pbufs[consume] = p;

		consume = (consume + 1) % NUM_RX_BUF;
	}

	LPC_EMAC->RxConsumeIndex = consume;
	eth_rx_read_index = readpos;
}

void eth_hardware_init(void) {

	/* Set up pins */
	int i;
	for (i = 0; i < (sizeof(ether_pins) / sizeof(ether_pins[0])); i++) {
		int shift = 2 * ether_pins[i];
		LPC_PINCON->PINSEL2 = (LPC_PINCON->PINSEL2 & ~(3 << shift)) | (1 << shift);
	}

	/* Set up Ethernet in autosense mode */
	while (EMAC_Init() == ERROR){
                 // Delay for a while then continue initializing EMAC module
                 outputf("Error during initializing EMAC, restart after a while");
		int delay;
		for (delay = 0x100000; delay; delay--);
         }
}
