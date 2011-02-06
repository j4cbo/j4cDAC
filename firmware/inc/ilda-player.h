/* j4cDAC ILDA player
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

#ifndef ILD_PLAYER_H
#define ILD_PLAYER_H

int ilda_open(const char * fname);
int ilda_read_points(int max_points, dac_point_t *p);
void ilda_reset_file(void);

void ilda_set_fps_limit(int max_fps);

extern int ilda_current_fps;

#endif
