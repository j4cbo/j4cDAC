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

#include <shlwapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <process.h>

#include <protocol.h>
#include "j4cDAC.h"

#define BUFFER_POINTS_PER_FRAME 16000
#define BUFFER_NFRAMES          2

/* Network connection
 */
typedef struct dac_conn_s {
	SOCKET sock;
	char buf[1024];
	int size;
} dac_conn_t;

/* Double buffer
 */
struct buffer_item {
	struct dac_point data[BUFFER_POINTS_PER_FRAME];
	int points;
	int pps;
	int repeatcount;
	int idx;
};

/* DAC
 */
typedef struct dac_s {
	CRITICAL_SECTION buffer_lock;
	struct buffer_item buffer[BUFFER_NFRAMES];
	int buffer_read, buffer_fullness;

	HANDLE workerthread;
	HANDLE loop_go;
	
	struct in_addr addr;
	dac_conn_t conn;

	enum {
		ST_DISCONNECTED,
		ST_READY,
		ST_RUNNING,
		ST_BROKEN,
		ST_SHUTDOWN
	} state;

	struct dac_s * next;
} dac_t;

void flog (char *fmt, ...);

/* dac.c */
int dac_init(dac_t *d);
int dac_open_connection(dac_t *d);
void dac_close_connection(dac_t *d);
dac_t *dac_get(int);
struct buffer_item *buf_get_write(dac_t *d);
void buf_advance_write(dac_t *d);
int dac_get_status(dac_t *d);
int do_write_frame(dac_t *d, const void * data, int bytes, int pps,
	int reps, int (*convert)(struct buffer_item *, const void *, int));

/* comm.c */
void log_socket_error(const char *call);
int dac_connect(dac_conn_t *conn, const char *host, const char *port);
int dac_send_data(dac_conn_t *conn, struct dac_point *data, int npoints, int rate);
const struct dac_status * dac_last_status(void);
int dac_outstanding_acks(void);

#endif
