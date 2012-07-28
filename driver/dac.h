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

#include <windows.h>
#include <shlwapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <process.h>
#include <time.h>

#include <protocol.h>
#include "j4cDAC.h"

#define BUFFER_POINTS_PER_FRAME 16000
#define BUFFER_NFRAMES          2

#define MAX_LATE_ACKS		64

#define DAC_MIN_SEND		40

/* Network connection
 */
typedef struct dac_conn_s {
	SOCKET sock;
	SOCKET udp_sock;
	char buf[1024];
	int size;
	struct dac_response resp;
	LARGE_INTEGER last_ack_time;

	struct {
		struct queue_command queue;
		struct data_command_header header;
		struct dac_point data[1000];
	} __attribute__((packed)) local_buffer;

	int begin_sent;
	int ackbuf[MAX_LATE_ACKS];
	int ackbuf_prod;
	int ackbuf_cons;
	int unacked_points;
	int pending_meta_acks;
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

enum dac_state {
	ST_DISCONNECTED,
	ST_READY,
	ST_RUNNING,
	ST_BROKEN,
	ST_SHUTDOWN
};

/* DAC
 */
typedef struct dac_s {
	CRITICAL_SECTION buffer_lock;
	struct buffer_item buffer[BUFFER_NFRAMES];
	int buffer_read, buffer_fullness;
	int bounce_count;

	HANDLE workerthread;
	HANDLE loop_go;
	
	struct in_addr addr;
	dac_conn_t conn;
	int32_t dac_id;
	int sw_revision;
	char mac_address[6];

	enum dac_state state;

	char version[32];

	struct dac_s * next;
} dac_t;

void trace (dac_t *d, char *fmt, ...);

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
void dac_get_name(dac_t *d, char *buf, int max);
int dac_get_acks(dac_t *d, int wait);

/* comm.c */
void log_socket_error(dac_t *d, const char *call);
int dac_connect(dac_t *d, const char *host, const char *port);
int dac_disconnect(dac_t *d);
int dac_send_data(dac_t *d, struct dac_point *data, int npoints, int rate);

extern LARGE_INTEGER timer_freq;
#endif
