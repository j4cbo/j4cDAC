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

typedef struct dac_point {
	uint16_t control;
	int16_t x;
	int16_t y;
	uint16_t r;
	uint16_t g;
	uint16_t b;
	uint16_t i;
	uint16_t u1;
	uint16_t u2;
} dac_point_t;

enum dac_state {
	DAC_IDLE = 0,
	DAC_PREPARED = 1,
	DAC_PLAYING = 2
};

#define DAC_BUFFER_POINTS	1800

/* This is the maximum point rate that we advertise in our periodic
 * broadcast packet. 100kpps ought to be enough for anyone. */
#define DAC_MAX_POINT_RATE	100000

void dac_init(void);

int dac_prepare(void);
int dac_start(int points_per_second);
int dac_request(dac_point_t **addrp);
void dac_advance(int count);
void dac_stop(void);
enum dac_state dac_get_state(void);
int dac_fullness(void);

extern int dac_current_pps;
extern int dac_count;

#endif
