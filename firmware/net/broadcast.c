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
#include <playback.h>
#include <hardware.h>
#include <string.h>

#define BROADCAST_PORT	7654

static struct udp_pcb broadcast_pcb;

/* fill_status
 *
 * Fill in a struct dac_status with the current state of things.
 */
void fill_status(struct dac_status *status) {
	status->light_engine_state = le_get_state();
	status->playback_state = dac_get_state();
	status->playback_flags = dac_flags;
	status->light_engine_flags = le_get_flags();
	status->buffer_fullness = dac_fullness();

	/* Only report a point rate if currently playing */
	if (status->playback_state == DAC_PLAYING)
		status->point_rate = dac_current_pps;
	else
		status->point_rate = 0;

	status->point_count = dac_count;

	status->source = playback_src;
	status->source_flags = 0;	// XXX TODO
}

/* broadcast_send
 *
 * Fire off a broadcast packet with information about this DAC.
 */
void broadcast_send(void) {
	/* Because lwip is an enormous steaming pile of the finest software
	 * engineering, it is not possible to just allocate *one* pbuf
	 * during initialization - udp_send modifies the pbuf it is given
	 * and changes, among other things, its total length. (??!) So we
	 * allocatea fresh one each time.
	 */

	udp_new(&broadcast_pcb);

	udp_bind(&broadcast_pcb, IP_ADDR_ANY, BROADCAST_PORT);

	udp_connect(&broadcast_pcb, IP_ADDR_BROADCAST, BROADCAST_PORT);

	struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct
		dac_broadcast), PBUF_RAM);

	/* Shamefully bail out. */
	if (!p)
		return;

	struct dac_broadcast *pkt = (struct dac_broadcast *) p->payload;

	memcpy(pkt->mac_address, mac_address, sizeof(mac_address));
	fill_status(&pkt->status);
	pkt->buffer_capacity = DAC_BUFFER_POINTS - 1;
	pkt->max_point_rate = DAC_MAX_POINT_RATE;

	pkt->hw_revision = hw_board_rev;
	pkt->sw_revision = 1;	// XXX TODO - integrate into build system

	udp_send(&broadcast_pcb, p);
	pbuf_free(p);

	udp_remove(&broadcast_pcb);
}
