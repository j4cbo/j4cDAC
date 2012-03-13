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

#define WINVER 0x0501

#include <process.h>
#include <shlwapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdint.h>

#include "dac.h"

#define DEFAULT_TIMEOUT	2000000

/* Compile-time assert macro.
 *
 * Source: http://www.pixelbeat.org/programming/gcc/static_assert.html
 */
#define ct_assert(e) ((void)sizeof(char[1 - 2*!(e)]))

int dac_sendall(dac_t *d, const char *data, int len);

/* Log a socket error to the j4cDAC driver log file.
 */
void log_socket_error(dac_t *d, const char *call) {
	char buf[80];
	int err = WSAGetLastError();

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, err, 0,
		buf, sizeof(buf), 0);

	flogd(d, "!! Socket error in %s: %d: %s\n", call, err, buf);
}


/* Wait for activity on one file descriptor.
 */
int wait_for_activity(dac_t *d, int usec) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(d->conn.sock, &set);
	struct timeval time;
	time.tv_sec = usec / 1000000;
	time.tv_usec = usec % 1000000;
	int res = select(0, &set, NULL, &set, &time);

	if (res == SOCKET_ERROR) {
		log_socket_error(d, "select");
		return -1;
	}

	return res;
}

/* Wait for writability.
 */
int wait_for_write(dac_t *d, int usec) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(d->conn.sock, &set);
	struct timeval time;
	time.tv_sec = usec / 1000000;
	time.tv_usec = usec % 1000000;
	int res = select(0, NULL, &set, &set, &time);

	if (res == SOCKET_ERROR) {
		log_socket_error(d, "select");
		return -1;
	}

	return res;
}

/* Read exactly n bytes from the DAC connection socket.
 *
 * This reads exactly len bytes into buf. Data is read in chunks from the
 * OS socket library and buffered in the dac_conn_t structure; individual
 * sections are then copied out.
 *
 * Returns 0 on success, -1 on error. If an error occurs, this will call
 * log_socket_error() to log the issue. The error code is also available
 * from WSAGetLastError().
 */ 
int dac_read_bytes(dac_t *d, char *buf, int len) {
	while (d->conn.size < len) {
		// Wait for readability.
		int res = wait_for_activity(d, DEFAULT_TIMEOUT);

		if (res < 0) {
			closesocket(d->conn.sock);
			d->conn.sock = INVALID_SOCKET;
			return -1;
		} else if (res == 0) {
			flogd(d, "!! Read from DAC timed out.\n");
			closesocket(d->conn.sock);
			d->conn.sock = INVALID_SOCKET;
			return -1;
		}

		res = recv(d->conn.sock, d->conn.buf + d->conn.size,
			len - d->conn.size, 0);

		if (res == 0 || res == SOCKET_ERROR) {
			log_socket_error(d, "recv");
			return -1;
		}

		d->conn.size += res;
	}

	memcpy(buf, d->conn.buf, len);
	if (d->conn.size > len) {
		memmove(d->conn.buf, d->conn.buf + len, d->conn.size - len);
	}
	d->conn.size -= len;

	return 0;
}


/* Read a response from the DAC into the dac_resp global
 *
 * This only reads the response structure. It does no parsing of the
 * result. All processing of dac_resp is the responsibility of the caller.
 *
 * Returns 0 on success, -1 on error. On error, log_socket_error() will
 * have been called.
 */
int dac_read_resp(dac_t *d, int timeout) {
	int res = dac_read_bytes(d, (char *)&d->conn.resp, sizeof(d->conn.resp));
	if (res < 0)
		return res;

	QueryPerformanceCounter(&d->conn.last_ack_time);

	return 0;
}


void dac_dump_resp(dac_t *d) {
	dac_conn_t *conn = &d->conn;
	struct dac_status *st = &conn->resp.dac_status;
	flogd(d, "Protocol %d / LE %d / playback %d / source %d\n",
		0 /* st->protocol */, st->light_engine_state,
		st->playback_state, st->source);
	flogd(d, "Flags: LE %x, playback %x, source %x\n",
		st->light_engine_flags, st->playback_flags,
		st->source_flags);
	flogd(d, "Buffer: %d points, %d pps, %d total played\n",
		st->buffer_fullness, st->point_rate, st->point_count);
}


/* Initialize a dac_conn_t and connect to the DAC.
 *
 * On success, return 0.
 */
int dac_connect(dac_t *d, const char *host, const char *port) {
	dac_conn_t *conn = &d->conn;
	ZeroMemory(conn, sizeof(d->conn));

	// Look up the server address
	struct addrinfo *result = NULL, *ptr = NULL, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	flogd(d, "Calling getaddrinfo: \"%s\", \"%s\"\n", host, port);

	int res = getaddrinfo(host, port, &hints, &result);
	if (res != 0) {
		flogd(d, "getaddrinfo failed: %d\n", res);
		return -1;
	}

	// Create a SOCKET
	ptr = result;
	conn->sock = socket(ptr->ai_family, ptr->ai_socktype, 
		ptr->ai_protocol);

	if (conn->sock == INVALID_SOCKET) {
		log_socket_error(d, "socket");
		freeaddrinfo(result);
		return -1;
	}

	unsigned long nonblocking = 1;
	ioctlsocket(conn->sock, FIONBIO, &nonblocking);

	// Connect to host. Because the socket is nonblocking, this
	// will always return WSAEWOULDBLOCK.
	connect(conn->sock, ptr->ai_addr, (int)ptr->ai_addrlen);
	freeaddrinfo(result);

	if (WSAGetLastError() != WSAEWOULDBLOCK) {
		log_socket_error(d, "connect");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	// Wait for connection.
	fd_set set;
	FD_ZERO(&set);
	FD_SET(conn->sock, &set);
	struct timeval time;
	time.tv_sec = 0;
	time.tv_usec = DEFAULT_TIMEOUT;
	res = select(0, &set, &set, &set, &time);

	if (res == SOCKET_ERROR) {
		log_socket_error(d, "select");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	} else if (res == 0) {
		flogd(d, "Connection to %s timed out.\n", host);
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	// See if we have *actually* connected
	int error;
	int len = sizeof(error);
	if (getsockopt(conn->sock, SOL_SOCKET, SO_ERROR, (char *)&error, &len) ==
			SOCKET_ERROR) {
		log_socket_error(d, "getsockopt");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	if (error) {
		WSASetLastError(error);
		log_socket_error(d, "connect");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	BOOL ndelay = 1;
	res = setsockopt(conn->sock, IPPROTO_TCP, TCP_NODELAY,
		(char *)&ndelay, sizeof(ndelay));
	if (res == SOCKET_ERROR) {
		log_socket_error(d, "setsockopt");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	flogd(d, "Connected.\n");

	// After we connect, the host will send an initial status response. 
	dac_read_resp(d, DEFAULT_TIMEOUT);
	dac_dump_resp(d);

	char c = 'p';
	dac_sendall(d, &c, 1);
	dac_read_resp(d, DEFAULT_TIMEOUT);
	dac_dump_resp(d);

	flogd(d, "Sent.\n");

	return 0;
}

int dac_disconnect(dac_t *d) {
	closesocket(d->conn.sock);
	d->conn.sock = INVALID_SOCKET;
	return 0;
}

int dac_sendall(dac_t *d, const char *data, int len) {
	do {
		int res = wait_for_write(d, 1500000);
		if (res < 0) {
			return -1;
		} else if (res == 0) {
			flogd(d, "write timed out\n");
		}

		res = send(d->conn.sock, data, len, 0);

		if (res == SOCKET_ERROR) {
			log_socket_error(d, "send");
			return -1;
		}

		len -= res;
		data += res;
	} while (len);

	return 0;
}

int check_data_response(dac_t *d) {
	dac_conn_t *conn = &d->conn;
	if (conn->resp.dac_status.playback_state == 0)
		conn->begin_sent = 0;

	if (conn->resp.command == 'd') {
		if (conn->ackbuf_prod == conn->ackbuf_cons) {
			flogd(d, "!! Protocol error: didn't expect data ack\n");
			return -1;
		}
		conn->unacked_points -= conn->ackbuf[conn->ackbuf_cons];
		conn->ackbuf_cons = (conn->ackbuf_cons + 1) % MAX_LATE_ACKS;
	} else {
		conn->pending_meta_acks--;
	}

	if (conn->resp.response != 'a' && conn->resp.response != 'I') {
		flogd(d, "!! Protocol error: ACK for '%c' got '%c' (%d)\n",
			conn->resp.command,
			conn->resp.response, conn->resp.response);
		return -1;
	}

	return 0;
}

int dac_send_data(dac_t *d, struct dac_point *data, int npoints, int rate) { 
	int res;
	const struct dac_status *st = &d->conn.resp.dac_status;

	/* Write the header */

	if (st->playback_state == 0) {
		flogd(d, "L: Sending prepare command...\n");
		char c = 'p';
		if ((res = dac_sendall(d, &c, sizeof(c))) < 0)
			return res;

		/* Read ACK */
		d->conn.pending_meta_acks++;

		/* Block here until all ACKs received... */
		while (d->conn.pending_meta_acks)
			dac_get_acks(d, 1500);

		flogd(d, "L: prepare ACKed\n");
	}

	if (st->buffer_fullness > 1600 && st->playback_state == 1 \
	    && !d->conn.begin_sent) {
		flogd(d, "L: Sending begin command...\n");

		struct begin_command b;
		b.command = 'b';
		b.point_rate = rate;
		b.low_water_mark = 0;
		if ((res = dac_sendall(d, (const char *)&b, sizeof(b))) < 0)
			return res;

		d->conn.begin_sent = 1;
		d->conn.pending_meta_acks++;
	}

	d->conn.local_buffer.queue.command = 'q';
	d->conn.local_buffer.queue.point_rate = rate;

	d->conn.local_buffer.header.command = 'd';
	d->conn.local_buffer.header.npoints = npoints;

	memcpy(&d->conn.local_buffer.data[0], data,
		npoints * sizeof(struct dac_point));

	d->conn.local_buffer.data[0].control |= DAC_CTRL_RATE_CHANGE;

	ct_assert(sizeof(d->conn.local_buffer) == 18008);

	/* Write the data */
	if ((res = dac_sendall(d, (const char *)&d->conn.local_buffer,
		8 + npoints * sizeof(struct dac_point))) < 0)
		return res;

	/* Expect two ACKs */
	d->conn.pending_meta_acks++;
	d->conn.ackbuf[d->conn.ackbuf_prod] = npoints;
	d->conn.ackbuf_prod = (d->conn.ackbuf_prod + 1) % MAX_LATE_ACKS;
	d->conn.unacked_points += npoints;

	if ((res = dac_get_acks(d, 0)) < 0)
		return res;

	return npoints;
}

int dac_get_acks(dac_t *d, int wait) {
	/* Read any ACKs we are owed */
	while (d->conn.pending_meta_acks
	       || (d->conn.ackbuf_prod != d->conn.ackbuf_cons)) {
		int res;
		if (wait_for_activity(d, wait)) { 
			if ((res = dac_read_resp(d, 1)) < 0)
				return res;
			if ((res = check_data_response(d)) < 0)
				return res;
		} else {
			break;
		}
	}
	return 0;
}

int loop(dac_conn_t *conn) {
	return 0;
}
