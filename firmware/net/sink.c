/* j4cDAC data sink - for performance testing
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


#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <tables.h>

static err_t sink_recv_FPV_tcp_recv(struct tcp_pcb * pcb, struct pbuf * pbuf,
                                    err_t err) {

	if (pbuf == NULL) {
		outputf("ps: connection closed");
		tcp_recv(pcb, NULL);
		tcp_close(pcb);
		return ERR_OK;
	}

	/* Tell lwIP we're done with this packet. */
	tcp_recved(pcb, pbuf->tot_len);
	pbuf_free(pbuf);

	return ERR_OK;
}

static err_t sink_accept_FPV_tcp_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
	LWIP_UNUSED_ARG(err);
	LWIP_UNUSED_ARG(arg);

	tcp_recv(pcb, sink_recv_FPV_tcp_recv);

	return ERR_OK;
}

void sink_init(void) {
	struct tcp_pcb *pcb;

	pcb = tcp_new();
	tcp_bind(pcb, IP_ADDR_ANY, 9);
	pcb = tcp_listen(pcb);
	tcp_accept(pcb, sink_accept_FPV_tcp_accept);
}

INITIALIZER(protocol, sink_init);
