#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1

#define LWIP_DEBUG	1
/*
#define DHCP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define AUTOIP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define TCP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define TCP_INPUT_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define MEMP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define PBUF_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define MEM_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE

*/

#define ARP_TABLE_SIZE	8

/* What do we enable? */
#define LWIP_SOCKET	0
#define LWIP_NETCONN	0
#define LWIP_SNMP	0
#define LWIP_AUTOIP	1
#define LWIP_DHCP_AUTOIP_COOP	1
#define LWIP_DHCP	1
#define LWIP_DHCP_AUTOIP_COOP_TRIES     1

/* For big ones... */
#define MEMCPY(dst, src, len) memcpy(dst, src, len)

/* We will never be sending IP packets larger than the MTU... Note that
 * this doesn't disable *reassembly*, just fragmenting outgoing stuff. */
#define IP_FRAG 0

/* Memory for listening TCP sockets.
 *
 * We will likely only use TCP for two protocols - the DAC point streaming
 * and HTTP. Everything else (OSC, ArtNet, status broadcasts...) is UDP.
 */
#define MEMP_NUM_TCP_PCB_LISTEN	2

#define TCP_MSS         1460
#define TCP_WND		10000
#define TCP_SND_BUF     (2 * TCP_MSS)
#define TCP_SND_QUEUELEN 8

#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 1
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"

#endif
