/* j4cDAC periodic broadcast
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

#include <broadcast.h>
#include <dac.h>
#include <lightengine.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <assert.h>
#include <ether.h>

#define BROADCAST_PORT	7654

static struct udp_pcb * broadcast_pcb;
static struct pbuf * broadcast_pbuf;

/* fill_status
 *
 * Fill in a struct dac_status with the current state of things.
 */
void fill_status(struct dac_status *status) {
	status->light_engine_state = le_get_state();
	status->playback_state = dac_get_state();
	status->light_engine_flags = le_get_flags();
	status->buffer_fullness = dac_fullness();
	status->point_rate = dac_current_pps;
	status->point_count = dac_count;

	status->playback_flags = 0;	// XXX TODO
	status->source = 0;	// XXX TODO
}

/* broadcast_init
 *
 * This must be called before the periodic DAC broadcasts begin.
 */
void broadcast_init(void) {
	broadcast_pcb = udp_new();

	ASSERT_NOT_NULL(broadcast_pcb);

	udp_bind(broadcast_pcb, IP_ADDR_ANY, BROADCAST_PORT);

	udp_connect(broadcast_pcb, IP_ADDR_BROADCAST, BROADCAST_PORT);

	broadcast_pbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct
		dac_broadcast), PBUF_RAM);
}

/* broadcast_send
 *
 * Fire off a broadcast packet with information about this DAC.
 */
void broadcast_send(void) {
	struct dac_broadcast *pkt = (struct dac_broadcast *)
		broadcast_pbuf->payload;

	eth_get_mac(pkt->mac_address);
	fill_status(&pkt->status);
	pkt->buffer_capacity = DAC_BUFFER_POINTS - 1;
	pkt->max_point_rate = DAC_MAX_POINT_RATE;

	pkt->hw_revision = 0;	// XXX TODO
	pkt->sw_revision = 1;	// XXX TODO - integrate into build system

	udp_send(broadcast_pcb, broadcast_pbuf);
}
