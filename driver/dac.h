/* j4cDAC communication library
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

#ifndef DAC_H
#define DAC_H

#include <protocol.h>

typedef struct dac_conn_s {
	SOCKET sock;
	char buf[1024];
	int size;
} dac_conn_t;

int dac_connect(dac_conn_t *conn, const char *host, const char *port);
int dac_send_data(dac_conn_t *conn, struct dac_point *data, int npoints, int rate);
const struct dac_status * dac_last_status(void);
int dac_outstanding_acks(void);

#endif
