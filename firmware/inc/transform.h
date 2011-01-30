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

#ifndef TRANSFORM_MAX

#include <stdint.h>

#define COORD_MAX	32768

#define COORD_TOO_CLOSE	6000

#define CORNER_TL	0
#define CORNER_TR	1
#define CORNER_BL	2
#define CORNER_BR	3

#define CORNER_FLIP_V(corner)	((corner) ^ 1)
#define CORNER_FLIP_H(corner)	((corner) ^ 2)

#define IS_TOP(corner)		(((corner) & 2) == 0)
#define IS_BOTTOM(corner)	(((corner) & 2) == 2)
#define IS_LEFT(corner)		(((corner) & 1) == 0)
#define IS_RIGHT(corner)	(((corner) & 1) == 1)

typedef int32_t transform[4];

extern transform transform_x, transform_y;

void update_transform();

int32_t translate_x(int32_t x, int32_t y);
int32_t translate_y(int32_t x, int32_t y);

#endif
