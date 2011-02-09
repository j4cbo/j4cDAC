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

static const uint8_t default_mac[] = { 0x10, 0x1f, 0xe0, 0x12, 0x1d, 0x0c };

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

	/* XXX I should have put a serial EEPROM on the board for MAC
	 * address storage. Oops. */
	memcpy(ether_netif.hwaddr, default_mac, 6);

	eth_hardware_init(ether_netif.hwaddr);

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
	netif_set_up(&ether_netif);
	dhcp_start(&ether_netif, &dhcp_state);
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

/* This is very hacky. */
void eth_get_mac(uint8_t *mac) {
	memcpy(mac, ether_netif.hwaddr, 6);
}

INITIALIZER(hardware, eth_init)
INITIALIZER(poll, eth_poll)
