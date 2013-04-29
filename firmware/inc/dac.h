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

#define DAC_BUFFER_POINTS	1800
#define DAC_INSTRUMENT_TIME	1

#ifndef __ASSEMBLER__

#include <protocol.h>

enum dac_state {
	DAC_IDLE = 0,
	DAC_PREPARED = 1,
	DAC_PLAYING = 2
};

#define DAC_FLAG_SHUTTER	(1 << 0)
#define DAC_FLAG_STOP_UNDERFLOW	(1 << 1)
#define DAC_FLAG_STOP_ESTOP	(1 << 2)
#define DAC_FLAG_STOP_SRCSWITCH	(1 << 3)
#define DAC_FLAG_STOP_ALL	0x0E

#define DAC_RATE_BUFFER_SIZE	200

/* This is the maximum point rate that we advertise in our periodic
 * broadcast packet. 100kpps ought to be enough for anyone. */
#define DAC_MAX_POINT_RATE	100000

typedef struct packed_point_t {
	int16_t x;
	int16_t y;
	uint32_t irg;
	uint32_t i12;
	uint16_t bf;
} __attribute__((packed)) __attribute__((aligned(2))) packed_point_t;

/* Pack a point from the DAC into a bufferable format */

static inline void dac_pack_point(packed_point_t *dest, dac_point_t *src) {
	/* IIRRRGGG
	 * 111222ix
	 * fBBB
	*/
	dest->x = src->x;
	dest->y = src->y;

	#define U(color) ((uint32_t)(src->color))
#ifndef __arm__
       dest->irg = (src->g >> 4) | ((U(r) & 0xFFF0) << 8) | ((U(i) & 0xFFF0) << 16);
       dest->i12 = (U(i) & 0x00F0) | ((U(u2) & 0xFFF0) << 4) | ((U(u1) & 0xFFF0) << 20);
       dest->bf = (src->b >> 4) | (src->control & 0xF000);
#else
	uint32_t irg;
	asm("bfi %0, %1, 16, 16" : "=r" (irg) : "r" (U(i)));
	asm("bfi %0, %1, 8, 16" : "+r" (irg) : "r" (U(r)));
	asm("bfc %0, 0, 12" : "+r" (irg));
	asm("orr %0, %0, %1, lsr 4" : "+r" (irg) : "r" (src->g));
	dest->irg = irg;

	uint32_t i12;
	asm("bfi %0, %1, 16, 16" : "=r" (i12) : "r" (U(u1)));
	asm("bfi %0, %1, 4, 16" : "+r" (i12) : "r" (U(u2)));
	uint32_t imask = U(i);
	asm("bfc %0, 8, 24" : "+r" (imask));
	asm("orr %0, %0, %1" : "+r" (i12) : "r" (imask));
	dest->i12 = i12;

	uint32_t control = src->control;
	asm("bfc %0, 0, 12" : "+r" (control));
	dest->bf = control | (src->b >> 4);
#endif
}

#define UNPACK_X(p)	((p)->x)
#define UNPACK_Y(p)	((p)->y)
#define UNPACK_R(p)	(((p)->irg >> 8) & 0xFFF0)
#define UNPACK_G(p)	(((p)->irg << 4) & 0xFFF0)
#define UNPACK_B(p)	(((p)->bf << 4) & 0xFFF0)
#define UNPACK_I(p)	((((p)->irg >> 16) & 0xFF00) | ((p)->i12 & 0xF0))
#define UNPACK_U1(p)	(((p)->i12 >> 4) & 0xFFF0)
#define UNPACK_U2(p)	(((p)->i12 >> 16) & 0xFFF0)

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
uint32_t dac_get_count();
void shutter_set(int state);

void delay_line_set_delay(int color_index, int delay);
int delay_line_get_delay(int color_index);
uint32_t color_corr_get_offset(int color_index);
uint32_t color_corr_get_gain(int color_index);
void color_corr_set_offset(int color_index, int32_t offset);
void color_corr_set_gain(int color_index, int32_t gain);

extern int dac_current_pps;
extern int dac_flags;

extern uint32_t dac_cycle_count;

#endif

#endif
