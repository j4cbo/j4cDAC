#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1

#define LWIP_DEBUG	1
#define DHCP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
/*
#define MEMP_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define PBUF_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
#define MEM_DEBUG	LWIP_DBG_ON | LWIP_DBG_TRACE
*/

/* We have our own malloc. */
#include <stdlib.h>
#define MEM_LIBC_MALLOC	1
#define MEMP_MEM_MALLOC	1

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
#define TCP_WND		24000
#define TCP_SND_BUF     (16 * TCP_MSS)
#define TCP_SND_QUEUELEN 8

/* This is only an array of descriptors - each is small. */
#define MEMP_NUM_PBUF	256

#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 1
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"

#endif
