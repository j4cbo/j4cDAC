/* j4cDAC built-in test patterns
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

#include <stdint.h>
#include <dac.h>
#include <serial.h>

static int tp_position;

extern const unsigned char ildatest_bts[7164];

void tp_trianglewave_run() {
	dac_point_t *ptr = 0;
	int dlen = dac_request(&ptr);

	if (dlen < 0) {
		outputf("*** UNDERFLOW ***");
		dac_prepare();
		return;
	}

	if (dlen < 10 && dac_get_state() != DAC_PLAYING) {
		dac_start(30000);
	}

	if (dlen == 0)
		return;

	int i;
	for (i = 0; i < dlen; i++) {
		ptr[i].x = tp_position;
		ptr[i].y = tp_position;
		ptr[i].r = 65535;
		ptr[i].g = 0;
		ptr[i].b = 0;
		ptr[i].i = 0;
		ptr[i].u1 = 0;
		ptr[i].u2 = 0;

		tp_position += 256;
	}

	dac_advance(dlen);
}

void tp_ilda_run() {
	dac_point_t *ptr = 0;
	int dlen = dac_request(&ptr);

	if (dlen < 0) {
		outputf("*** UNDERFLOW ***");
		dac_prepare();
		return;
	}

	if (dlen < 10 && dac_get_state() != DAC_PLAYING) {
		dac_start(30000);
	}

	if (dlen == 0)
		return;

	int ctr = tp_position, i;
	for (i = 0; i < dlen; i++) {
		const unsigned char *w = &ildatest_bts[ctr];
		uint16_t x = ((w[3] << 8) & 0xF000) | (w[4] << 4);
		uint16_t y = ((w[3] << 12) & 0xF000) | (w[5] << 4);
		ptr[i].x = x;
		ptr[i].y = y;
		ptr[i].r = w[0] << 4;
		ptr[i].g = w[1] << 4;
		ptr[i].b = w[2] << 4;
		ptr[i].i = 0;
		ptr[i].u1 = 0;
		ptr[i].u2 = 0;

		ctr += 6;

		if (ctr >= 7164)
			ctr = 0;
	}

	dac_advance(dlen);

	tp_position = i;
}
