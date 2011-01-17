/* j4cDAC skub allocator
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
 */

#ifndef SKUB_POOL_FIXED
#error "skub-zones.h should only be included by skub.c / skub.h"
#endif

#include <string.h>
#include <stdint.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <netif/etharp.h>
#include <ipv4/lwip/ip_frag.h>
#include <ipv4/lwip/autoip.h>

SKUB_POOL_FIXED(AUTOIP, struct autoip, 2)
SKUB_POOL_FIXED(PBUF, struct pbuf, 16)
SKUB_POOL_FIXED(PBUF_POOL, u1536, 16)
SKUB_POOL_FIXED(ARP_QUEUE, struct etharp_q_entry, 16)
SKUB_POOL_FIXED(REASSDATA, struct ip_reassdata, MEMP_NUM_REASSDATA)
SKUB_POOL_FIXED(TCP_PCB, struct tcp_pcb, MEMP_NUM_TCP_PCB)
SKUB_POOL_FIXED(TCP_PCB_LISTEN, struct tcp_pcb_listen, MEMP_NUM_TCP_PCB_LISTEN)
SKUB_POOL_FIXED(TCP_SEG, struct tcp_seg, MEMP_NUM_TCP_SEG)

SKUB_POOL_VAR(16, 4)
SKUB_POOL_VAR(128, 6)
SKUB_POOL_VAR(384, 3)
