/* j4cDAC point stream network protocol
 *
 * Copyright 2010, 2011 Jacob Potter
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

#ifndef POINT_STREAM_H
#define POINT_STREAM_H

#include <stdint.h>
#include <dac.h>
#include <lightengine.h>
#include <broadcast.h>

struct begin_command {
	uint8_t command;	/* 'b' (0x62) */
	uint16_t low_water_mark;
	uint32_t point_rate;
} __attribute__ ((packed));

struct queue_command {
	uint8_t command;	/* 'q' (0x74) */
	uint32_t point_rate;
} __attribute__ ((packed));

struct data_command {
	uint8_t command;	/* 'd' (0x64) */
	uint16_t npoints;
	struct dac_point data[];
} __attribute__ ((packed));

struct dac_response {
	uint8_t response;
	uint8_t command;
	struct dac_status dac_status;
};

#define CONNCLOSED_USER		(1)
#define CONNCLOSED_UNKNOWNCMD	(2)
#define CONNCLOSED_SENDFAIL	(3)
#define CONNCLOSED_MASK		(0xF)

#define RESP_ACK		'a'
#define RESP_NAK_FULL		'F'
#define RESP_NAK_INVL		'I'
#define RESP_NAK_ESTOP		'!'

void ps_init(void);

#endif
