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

#include <lightengine.h>
#include <dac.h>

enum le_state le_state;
uint16_t le_flags;

/* This is basically a stub at the moment, until thermal control is added. */

enum le_state le_get_state(void) {
	return le_state;
}

uint16_t le_get_flags(void) {
	return le_state;
}

void le_estop(uint16_t condition) {
	le_flags |= condition;
	le_state = LIGHTENGINE_ESTOP;
	dac_stop();
}

void le_estop_clear(uint16_t condition) {
	le_flags &= ~condition;
	if (!le_flags)
		le_state = LIGHTENGINE_READY;
}
