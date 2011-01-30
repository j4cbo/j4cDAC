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

transform transform_x = {
	[CORNER_TL] = -COORD_MAX,
	[CORNER_TR] = COORD_MAX,
	[CORNER_BL] = -COORD_MAX,
	[CORNER_BR] = COORD_MAX
};

transform transform_y = {
	[CORNER_TL] = COORD_MAX,
	[CORNER_TR] = COORD_MAX,
	[CORNER_BL] = -COORD_MAX,
	[CORNER_BR] = -COORD_MAX
};

int32_t transform_matrix[8];

static void calculate_transform(int32_t *c, int32_t *coords) {
	int tl = coords[CORNER_TL];
	int tr = coords[CORNER_TR];
	int bl = coords[CORNER_BL];
	int br = coords[CORNER_BR];
	c[3] = (tl + tr + bl + br) / 4;
	c[2] = c[3] - (tl + br) / 2;
	c[1] = c[3] - (br + bl) / 2;
        c[0] = -bl - c[1] + c[2] + c[3];
}

void update_transform() {
	calculate_transform(transform_matrix, transform_x);
	calculate_transform(transform_matrix + 4, transform_y);
}

INITIALIZER(hardware, update_transform)

static int32_t translate(int32_t *c, int x, int y) {
	int32_t xy_scale = x * y / COORD_MAX;
	return (c[0]*x + c[1]*y + c[2]*xy_scale) / COORD_MAX + c[3];
}

int32_t translate_x(int32_t x, int32_t y) {
	return translate(transform_matrix, x, y);
}

int32_t translate_y(int32_t x, int32_t y) {
	return translate(transform_matrix + 4, x, y);
}
