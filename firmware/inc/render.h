/* j4cDAC abstract generator
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

#ifndef RENDER_H
#define RENDER_H

#include "fixpoint.h"
#include "protocol.h"

#define PPS			30000
#define FPS			30
#define POINTS_PER_FRAME	(PPS/FPS)

#define PERSIST_POINTS		3000

#define MUL_DENOM		12

typedef struct {
	const char * name;
	fixed freq;
	uint32_t pos;
	int slave_multiplier;
} oscillator_t;

typedef struct {
	const char * name;
	uint32_t value;
} param_t;

oscillator_t * get_oscillator(const char * name);
param_t * get_param(const char * name);

int get_mul(int index);

extern oscillator_t osc_master;

extern oscillator_t * const oscillators[];
extern param_t * const params[];

void get_next_point(dac_point_t *p);

#endif
