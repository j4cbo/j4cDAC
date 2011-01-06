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

#ifndef BROADCAST_H
#define BROADCAST_H

#include <stdint.h>

struct dac_status {
	uint8_t light_engine_state;
	uint8_t playback_state;
	uint16_t light_engine_flags;
	uint16_t playback_flags;
	uint16_t buffer_fullness;
	uint32_t point_rate;
	uint32_t point_count;
	uint8_t source;
	uint16_t source_flags;
} __attribute__ ((packed));

struct dac_broadcast {
	uint8_t mac_address[6];
	uint16_t hw_revision;
	uint16_t sw_revision;
	uint16_t buffer_capacity;
	uint32_t max_point_rate;
        struct dac_status status;
};

void broadcast_init(void);
void fill_status(struct dac_status *status);
void broadcast_send(void);

#endif
