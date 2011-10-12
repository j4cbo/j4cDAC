/* j4cDAC DMA/DAC driver
 *
 * Copyright 2010 Jacob Potter
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

#ifndef DAC_H
#define DAC_H

#include <protocol.h>

enum dac_state {
	DAC_IDLE = 0,
	DAC_PREPARED = 1,
	DAC_PLAYING = 2
};

#define DAC_FLAG_SHUTTER	(1 << 0)
#define DAC_FLAG_STOP_UNDERFLOW	(1 << 1)
#define DAC_FLAG_STOP_ESTOP	(1 << 2)
#define DAC_FLAG_STOP_ALL	\
	(DAC_FLAG_STOP_UNDERFLOW | DAC_FLAG_STOP_ESTOP)

#define DAC_BUFFER_POINTS	1800

#define DAC_RATE_BUFFER_SIZE	120

/* This is the maximum point rate that we advertise in our periodic
 * broadcast packet. 100kpps ought to be enough for anyone. */
#define DAC_MAX_POINT_RATE	100000

typedef struct packed_point_t {
	uint32_t words[3];
	uint16_t control;
} __attribute__((packed)) packed_point_t;

/* Pack a point from the DAC into a bufferable format */

static inline void dac_pack_point(packed_point_t *dest, dac_point_t *src) {
	/*
	   XXXYYYRR
	   RGGGBBBI
	   II111222
	*/
	dest->control = src->control;
	dest->words[0] = (((uint32_t)src->x & 0xFFF0) << 16)
		| ((src->y & 0xFFF0) << 4) | (src->r >> 8);
	dest->words[1] = (((uint32_t)src->r & 0xFFF0) << 24) | (((uint32_t)src->g & 0xFFF0) << 12)
		| (src->b & 0xFFF0) | ((src->i & 0xFFF0) >> 12);
	dest->words[2] = (((uint32_t)src->i & 0xFFF0) << 20) | (((uint32_t)src->u1 & 0xFFF0) << 8)
		| (src->u2 >> 4);
}

#define UNPACK_X(p)	(((p)->words[0] >> 20) & 0xFFF)
#define UNPACK_Y(p)	(((p)->words[0] >> 8) & 0xFFF)
#define UNPACK_R(p)	((((p)->words[0] << 4) & 0xFF0) | ((p)->words[1] >> 28))
#define UNPACK_G(p)	(((p)->words[1] >> 16) & 0xFFF)
#define UNPACK_B(p)	(((p)->words[1] >> 4) & 0xFFF)
#define UNPACK_I(p)	((((p)->words[1] << 8) & 0xF00) | ((p)->words[2] >> 24))
#define UNPACK_U1(p)	(((p)->words[2] >> 12) & 0xFFF)
#define UNPACK_U2(p)	(((p)->words[2] >> 0) & 0xFFF)

void dac_init(void);

int dac_prepare(void);
int dac_start(void);
int dac_request(void);
packed_point_t *dac_request_addr(void);
void dac_advance(int count);
void dac_stop(int flags);
enum dac_state dac_get_state(void);
int dac_fullness(void);
int dac_set_rate(int points_per_second);
int dac_rate_queue(int points_per_second);

extern int dac_current_pps;
extern int dac_count;
extern int dac_flags;

#endif
