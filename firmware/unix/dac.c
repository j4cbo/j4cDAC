/* j4cDAC DAC shim
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

#include <dac.h>
#include <stdio.h>
#include <unistd.h>
#include <serial.h>

int dac_set_rate(int points_per_second) {
	outputf("<dac> set rate %d\n", points_per_second);
	return 0;
}

int dac_start(void) {
	outputf("<dac> start\n");
	return 0;
}

int dac_rate_queue(int points_per_second) {
	outputf("<dac> queue rate %d\n", points_per_second);
	return 0;
}

int dac_request(void) {
	usleep(5000);
	return 0;
}

packed_point_t *dac_request_addr(void) {
	return NULL;
}

void dac_advance(int count) {
	outputf("<dac> advance %d\n", count);
}

void dac_init() {
}

int dac_prepare(void) {
	outputf("<dac> prepare\n");
	return 0;
}

void dac_stop(int flags) {
	outputf("<dac> stop %d\n", flags);
}

enum dac_state dac_get_state(void) {
	return DAC_IDLE;
}

int dac_fullness(void) {
	return 0;
}

void shutter_set(int state) {
}
