/* j4cDAC DP83848 bit definitions
 *
 * Copyright 2011 Jacob Potter
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
 *
 * This contains the portions of the Ethernet code not derived from NXP's
 * examples.
 */

#include <LPC17xx.h>
#include <panic.h>
#include <lightengine.h>
#include <netif/etharp.h>
#include <lwip/dhcp.h>
#include <ether.h>
#include <string.h>
#include <mdio.h>
#include <dp83848.h>
#include <tables.h>

static struct netif ether_netif;
static struct dhcp dhcp_state;

extern uint8_t mac_address[6];

static enum {
	LINK_DOWN,
	LINK_UP,
	LINK_GOING_DOWN
} eth_link_state;

void EINT3_IRQHandler(void) {
	if (!(LPC_GPIOINT->IO0IntStatF & (1 << 8))) {
		panic("Unexpected EINT3");
	}

	/* Mark the interrupt as handled */
	LPC_GPIOINT->IO0IntClr = (1 << 8);

	/* Once we're done with init, we only ever talk to MDIO from this
	 * interrupt handler, so it's safe to do so from interrupt context.
	 *
	 * Read MISR to clear the interrupt flags.
	 */
	mdio_read(DP83848_MISR);

	if (!(mdio_read(DP83848_PHYSTS) & (1 << 0))) {
		le_estop(ESTOP_LINKLOST);
		eth_link_state = LINK_GOING_DOWN;
	}
}

void eth_init() {
	struct ip_addr ipa = { 0 } , netmask = { 0 } , gw = { 0 };

	/* Set up basic fields in ether_netif */
	ether_netif.output = etharp_output_FPV_netif_output;
	ether_netif.linkoutput = eth_transmit_FPV_netif_linkoutput;
	ether_netif.name[0] = 'e';
	ether_netif.name[1] = 'n';
	ether_netif.hwaddr_len = ETHARP_HWADDR_LEN;
	ether_netif.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	ether_netif.mtu = 1500;

	memcpy(ether_netif.hwaddr, mac_address, 6);

	eth_link_state = LINK_DOWN;
	eth_hardware_init();

	/* Enable PHY interrupts */
	mdio_write(DP83848_MICR, 0x3);
	/* Interrupt on change of link status */
	mdio_write(DP83848_MISR, (1 << 5));

	/* Enable LPC1758 interrupt */
	LPC_GPIOINT->IO0IntEnF |= (1 << 8);
	NVIC_SetPriority(EINT3_IRQn, 1);
	NVIC_EnableIRQ(EINT3_IRQn);

	/* Hand it over to lwIP */
	netif_add(&ether_netif, &ipa, &netmask, &gw, NULL, ethernet_input);
	netif_set_default(&ether_netif);

	/* When the link comes up, we'l start DHCP. */
}

void handle_packet(struct pbuf *p) {

	struct eth_hdr *ethhdr = p->payload;

	if (ntohs(ethhdr->type) < 1500) {
		/* Ignore non-ethertype packets. */
		pbuf_free(p);
		return;
	}

	switch (ntohs(ethhdr->type)) {
	case ETHTYPE_IP:
	case ETHTYPE_ARP:
		ethernet_input(p, &ether_netif);
		break;

	case 0x86dd:
		/* Ignore IPv6. */
		pbuf_free(p);
		break;

	default:
		outputf("ethertype %04x", ntohs(ethhdr->type));
		pbuf_free(p);
		break;
	}
}

/* eth_check_link
 *
 * Manage the Ethernet link state - we need to tell LWIP when the link
 * goes up (so that it can start DHCP, etc) and when it goes down.
 */
void eth_check_link(void) {
	if (eth_link_state == LINK_UP) {
		return;
	} else if (eth_link_state == LINK_GOING_DOWN) {
		outputf("Ethernet link down");
		dhcp_stop(&ether_netif);
		netif_set_down(&ether_netif);
		eth_link_state = LINK_DOWN;
	} else {
		/* Is it up yet? */
		if (EMAC_UpdatePHYStatus() < 0) {
			/* Still no link. */
			return;
		}

		eth_link_state = LINK_UP;
		outputf("Ethernet link up");

		netif_set_up(&ether_netif);
		dhcp_start(&ether_netif, &dhcp_state);
	}
}

/* This is very hacky. */
void eth_get_mac(uint8_t *mac) {
	memcpy(mac, ether_netif.hwaddr, 6);
}

INITIALIZER(hardware, eth_init)
INITIALIZER(poll, eth_poll_1)
INITIALIZER(poll, eth_poll_2)
