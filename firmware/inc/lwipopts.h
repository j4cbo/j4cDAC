#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1

#define LWIP_DEBUG	1
/*
#define DHCP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define TCP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define TCP_INPUT_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define MEMP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define PBUF_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define MEM_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE

*/

/* Use LWIP's pool allocator. */
#include <stdlib.h>
#define MEMP_NUM_PBUF   32
#define PBUF_POOL_SIZE  16
#define PBUF_POOL_BUFSIZE 1536


/* What do we enable? */
#define LWIP_SOCKET	0
#define LWIP_NETCONN	0
#define LWIP_SNMP	0
#define LWIP_DHCP	1

/* For big ones... */
#define MEMCPY(dst, src, len) memcpy(dst, src, len)

/* We will never be sending IP packets larger than the MTU... Note that
 * this doesn't disable *reassembly*, just fragmenting outgoing stuff. */
#define IP_FRAG 0

/* Lots of tricks from http://lists.gnu.org/archive/html/lwip-users/2006-11/msg00007.html */
#define TCP_MSS         1460
#define TCP_WND		16000
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
