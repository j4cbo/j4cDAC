/* Ether Dream static IP configuration
 *
 * Copyright 2013 Jacob Potter
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

#include <serial.h>
#include <tables.h>
#include <osc.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/netif.h"
#include "lwip/dhcp.h"

extern struct netif ether_netif;
extern uint8_t ether_manual_ip;

static void net_ipaddr_FPV_param(const char *path, const char *ip) {
	dhcp_stop(&ether_netif);
	ether_manual_ip = 1;
	struct in_addr addr;
	if (inet_aton(ip, &addr)) {
		netif_set_ipaddr(&ether_netif, (struct ip_addr *)&addr);
	} else {
		outputf("bad ip %s", ip);
	}
}
static void net_netmask_FPV_param(const char *path, const char *ip) {
	dhcp_stop(&ether_netif);
	ether_manual_ip = 1;
	struct in_addr addr;
	if (inet_aton(ip, &addr)) {
		netif_set_netmask(&ether_netif, (struct ip_addr *)&addr);
	} else {
		outputf("bad netmask %s", ip);
	}
}
static void net_gateway_FPV_param(const char *path, const char *ip) {
	dhcp_stop(&ether_netif);
	ether_manual_ip = 1;
	struct in_addr addr;
	if (inet_aton(ip, &addr)) {
		netif_set_gw(&ether_netif, (struct ip_addr *)&addr);
	} else {
		outputf("bad gateway %s", ip);
	}
}

TABLE_ITEMS(param_handler, ifconfig_params,
	{ "/net/ipaddr", PARAM_TYPE_S1, { .fs = net_ipaddr_FPV_param } },
	{ "/net/netmask", PARAM_TYPE_S1, { .fs = net_netmask_FPV_param } },
	{ "/net/gateway", PARAM_TYPE_S1, { .fs = net_gateway_FPV_param } },
)
