/* j4cDAC light engine state machine
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

#ifndef LIGHTENGINE_H
#define LIGHTENGINE_H

#include <stdint.h>

enum le_state {
	LIGHTENGINE_READY = 0,
	LIGHTENGINE_WARMUP = 1,
	LIGHTENGINE_COOLDOWN = 2,
	LIGHTENGINE_ESTOP = 2,
};

#define ESTOP_PACKET		(1<<0)
#define ESTOP_INPUT		(1<<1)
#define ESTOP_INPUT_CUR		(1<<2)
#define ESTOP_OVERTEMP		(1<<3)
#define ESTOP_OVERTEMP_CUR	(1<<4)
#define ESTOP_LINKLOST		(1<<5)

#define ESTOP_CLEAR_ALL		(ESTOP_PACKET | ESTOP_INPUT | ESTOP_OVERTEMP)

void le_init(void);
void le_estop(uint16_t condition);
void le_estop_clear(uint16_t condition);

enum le_state le_get_state(void);
uint16_t le_get_flags(void);

#endif
