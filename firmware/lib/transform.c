/* j4cDAC geometric corrector
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
#include <transform.h>
#include <tables.h>
#include <serial.h>
#include <dac_settings.h>

/* We use a four-point perspective transformation here. The transform
 * is given by:
 *
 * x' = c1x + c2y + c3xy + c4
 * y' = c5x + c6y + c7xy + c8
 *
 * We let x=-n be "left", x=n be "right", y=-n be "bottom", and y=n be
 * "top". Four input points are given, so we have the following equations
 * to solve for our unknowns:
 *
 * x_bl = c1(-n) + c2(-n) + c3(-n)(-n) + c4
 * y_bl = c5(-n) + c6(-n) + c7(-n)(-n) + c8
 * x_br = c1(n) + c2(-n) + c3(n)(-n) + c4
 * y_br = c5(n) + c6(-n) + c7(n)(-n) + c8
 * x_tl = c1(-n) + c2(n) + c3(-n)(n) + c4
 * y_tl = c5(-n) + c6(n) + c7(-n)(n) + c8
 * x_tr = c1(n) + c2(n) + c3(n)(n) + c4
 * y_tr = c5(n) + c6(n) + c7(n)(n) + c8
 *
 * Or:
 * -c1n - c2n + c3n^2 + c4 = x_bl
 *  c1n - c2n - c3n^2 + c4 = x_br
 * -c1n + c2n - c3n^2 + c4 = x_tl
 *  c1n + c2n + c3n^2 + c4 = x_tr
 *
 * Putting this into triangular form, step 1:
 * -c1n - c2n + c3n^2 + c4 = x_bl
 *      -2c2n         +2c4 = x_br + x_bl
 *       2c2n -2c3n^2      = x_tl - x_bl
 *             2c3n^2 +2c4 = x_tr + x_bl
 *
 * Step 2:
 * -c1n - c2n + c3n^2 + c4 = x_bl
 *      -2c2n         +2c4 = x_br + x_bl
 *            -2c3n^2 +2c4 = x_tl + x_br
 *             2c3n^2 +2c4 = x_tr + x_bl
 *
 * Step 3:
 * -c1n - c2n + c3n^2 + c4 = x_bl
 *      -2c2n         +2c4 = x_br + x_bl
 *            -2c3n^2 +2c4 = x_tl + x_br
 *                    +4c4 = x_tr + x_bl + x_tl + x_br
 *
 * So:
 * c4 = (x_tr + x_bl + x_tl + x_br) / 4
 * c3n^2 = c4 - (x_tl + x_br) / 2
 * c2 = c4 - (x_br + x_bl) / 2
 * c1 = x_bl - c2 + c3 + c4
 */

int32_t transform_matrix[8];

static void calculate_transform(int32_t *c, int32_t *coords) {
	int tl = coords[CORNER_TL];
	int tr = coords[CORNER_TR];
	int bl = coords[CORNER_BL];
	int br = coords[CORNER_BR];
	c[6] = (tl + tr + bl + br) / 4;
	c[4] = c[6] - (tl + br) / 2;
	c[2] = c[6] - (br + bl) / 2;
	c[0] = -bl - c[2] + c[4] + c[6];
	c[6] += 0x8000;
}

void update_transform(void) {
	calculate_transform(transform_matrix, settings.transform_x);
	calculate_transform(transform_matrix + 1, settings.transform_y);

/*
	outputf("TL: %d %d\tTR: %d %d",
		settings.transform_x[CORNER_TL], settings.transform_y[CORNER_TL],
		settings.transform_x[CORNER_TR], settings.transform_y[CORNER_TR]);
	outputf("BL: %d %d\tBR: %d %d",
		settings.transform_x[CORNER_BL], settings.transform_y[CORNER_BL],
		settings.transform_x[CORNER_BR], settings.transform_y[CORNER_BR]);
*/
}

void init_transform(void) {
	/* XXX Fix this to load transform from i2c EEPROM */
	settings.transform_x[CORNER_TL] = -COORD_MAX;
	settings.transform_x[CORNER_TR] = COORD_MAX;
	settings.transform_x[CORNER_BL] = -COORD_MAX;
	settings.transform_x[CORNER_BR] = COORD_MAX;
	settings.transform_y[CORNER_TL] = COORD_MAX;
	settings.transform_y[CORNER_TR] = COORD_MAX;
	settings.transform_y[CORNER_BL] = -COORD_MAX;
	settings.transform_y[CORNER_BR] = -COORD_MAX;
	update_transform();
}

INITIALIZER(hardware, init_transform)
