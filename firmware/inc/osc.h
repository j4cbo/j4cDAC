/* j4cDAC OSC interface
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

#ifndef OSC_H
#define OSC_H

#include <stdint.h>
#include <param.h>

void osc_init(void);

void osc_send_int(const char *path, uint32_t value);
void osc_send_int2(const char *path, uint32_t v1, uint32_t v2);
void osc_send_fixed2(const char *path, fixed v1, fixed v2);
void osc_send_string(const char *path, const char *value);

#endif
