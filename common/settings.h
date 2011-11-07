/* j4cDAC configurable state
 *
 * Copyright 2011 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or 3, or the GNU Lesser General Public License version 3, as published
 * by the Free Software Foundation, at your option.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct dac_settings_s {
	/* IP config. To use DHCP, set ip_addr to 0.0.0.0; other fields will
	 * then be ignored. */
	uint8_t ip_addr[4];
	uint8_t ip_netmask[4];
	uint8_t ip_gateway[4];

	/* Color delay lines */
	uint8_t r_delay;
	uint8_t g_delay;
	uint8_t b_delay;
	uint8_t i_delay;

	/* Geometric correction */
	int32_t transform_x[4];
	int32_t transform_y[4];

} dac_settings_t;

#endif
