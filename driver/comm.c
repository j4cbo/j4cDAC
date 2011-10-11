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

int dac_sendall(dac_conn_t *conn, void *data, int len);

/* Log a socket error to the j4cDAC driver log file.
 */
void log_socket_error(const char *call) {
	char buf[80];
	int err = WSAGetLastError();

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, err, 0,
		buf, sizeof(buf), 0);

	flog("!! Socket error in %s: %d: %s\n", call, err, buf);
}


/* Wait for activity on one file descriptor.
 */
int wait_for_activity(SOCKET fd, int usec) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	struct timeval time;
	time.tv_sec = usec / 1000000;
	time.tv_usec = usec % 1000000;
	int res = select(0, &set, NULL, &set, &time);

	if (res == SOCKET_ERROR) {
		log_socket_error("select");
		return -1;
	}

	return res;
}

/* Wait for writability.
 */
int wait_for_write(SOCKET fd, int usec) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	struct timeval time;
	time.tv_sec = usec / 1000000;
	time.tv_usec = usec % 1000000;
	int res = select(0, NULL, &set, &set, &time);

	if (res == SOCKET_ERROR) {
		log_socket_error("select");
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
int dac_read_bytes(dac_conn_t *conn, char *buf, int len) {
	while (conn->size < len) {
		// Wait for readability.
		int res = wait_for_activity(conn->sock, DEFAULT_TIMEOUT);

		if (res < 0) {
			closesocket(conn->sock);
			conn->sock = INVALID_SOCKET;
			return -1;
		} else if (res == 0) {
			flog("!! Read from DAC timed out.\n");
			closesocket(conn->sock);
			conn->sock = INVALID_SOCKET;
			return -1;
		}

		res = recv(conn->sock, conn->buf + conn->size,
			len - conn->size, 0);

		if (res == 0 || res == SOCKET_ERROR) {
			log_socket_error("recv");
			return -1;
		}

		conn->size += res;
	}

	memcpy(buf, conn->buf, len);
	if (conn->size > len) {
		memmove(conn->buf, conn->buf + len, conn->size - len);
	}
	conn->size -= len;

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
int dac_read_resp(dac_conn_t *conn, int timeout) {
	int res = dac_read_bytes(conn, (char *)&conn->resp, sizeof(conn->resp));
	if (res < 0)
		return res;

	QueryPerformanceCounter(&conn->last_ack_time);

	return 0;
}


void dac_dump_resp(dac_conn_t *conn) {
	struct dac_status *st = &conn->resp.dac_status;
	flog("Protocol %d / LE %d / playback %d / source %d\n",
		0 /* st->protocol */, st->light_engine_state,
		st->playback_state, st->source);
	flog("Flags: LE %x, playback %x, source %x\n",
		st->light_engine_flags, st->playback_flags,
		st->source_flags);
	flog("Buffer: %d points, %d pps, %d total played\n",
		st->buffer_fullness, st->point_rate, st->point_count);
}


/* Initialize a dac_conn_t and connect to the DAC.
 *
 * On success, return 0.
 */
int dac_connect(dac_conn_t *conn, const char *host, const char *port) {
	ZeroMemory(conn, sizeof(*conn));

	// Look up the server address
	struct addrinfo *result = NULL, *ptr = NULL, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	flog("Calling getaddrinfo: \"%s\", \"%s\"\n", host, port);

	int res = getaddrinfo(host, port, &hints, &result);
	if (res != 0) {
		flog("getaddrinfo failed: %d\n", res);
		return -1;
	}

	// Create a SOCKET
	ptr = result;
	conn->sock = socket(ptr->ai_family, ptr->ai_socktype, 
		ptr->ai_protocol);

	if (conn->sock == INVALID_SOCKET) {
		log_socket_error("socket");
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
		log_socket_error("connect");
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
		log_socket_error("select");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	} else if (res == 0) {
		flog("Connection to %s timed out.\n", host);
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	// See if we have *actually* connected
	int error;
	int len = sizeof(error);
	if (getsockopt(conn->sock, SOL_SOCKET, SO_ERROR, (char *)&error, &len) ==
			SOCKET_ERROR) {
		log_socket_error("getsockopt");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	if (error) {
		WSASetLastError(error);
		log_socket_error("connect");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	BOOL ndelay = 1;
	res = setsockopt(conn->sock, IPPROTO_TCP, TCP_NODELAY,
		(char *)&ndelay, sizeof(ndelay));
	if (res == SOCKET_ERROR) {
		log_socket_error("setsockopt");
		closesocket(conn->sock);
		conn->sock = INVALID_SOCKET;
		return -1;
	}

	flog("Connected.\n");

	// After we connect, the host will send an initial status response. 
	dac_read_resp(conn, DEFAULT_TIMEOUT);
	dac_dump_resp(conn);

	char c = 'p';
	dac_sendall(conn, &c, 1);
	dac_read_resp(conn, DEFAULT_TIMEOUT);
	dac_dump_resp(conn);

	flog("Sent.\n");

	return 0;
}

int dac_disconnect(dac_conn_t *conn) {
	closesocket(conn->sock);
	conn->sock = INVALID_SOCKET;
	return 0;
}

int dac_sendall(dac_conn_t *conn, void *data, int len) {
	do {
		int res = wait_for_write(conn->sock, 1500000);
		if (res < 0) {
			return -1;
		} else if (res == 0) {
			flog("write timed out\n");
		}

		res = send(conn->sock, data, len, 0);

		if (res == SOCKET_ERROR) {
			log_socket_error("send");
			return -1;
		}

		len -= res;
		data += res;
	} while (len);

	return 0;
}

int check_data_response(dac_conn_t *conn) {
	if (conn->resp.dac_status.playback_state != 2)
		conn->begin_sent = 0;

	if (conn->resp.command == 'd') {
		if (conn->ackbuf_prod == conn->ackbuf_cons) {
			flog("!! Protocol error: didn't expect data ack");
			return -1;
		}
		conn->unacked_points -= conn->ackbuf[conn->ackbuf_cons];
		conn->ackbuf_cons = (conn->ackbuf_cons + 1) % MAX_LATE_ACKS;
	} else {
		conn->pending_meta_acks--;
	}

	if (conn->resp.response != 'a' && conn->resp.response != 'I') {
		flog("!! Protocol error: ACK for '%c' got '%c' (%d)\n",
			conn->resp.command,
			conn->resp.response, conn->resp.response);
		return -1;
	}

	return 0;
}

struct {
	struct queue_command queue;
	struct data_command_header header;
	struct dac_point data[1000];
} __attribute__((packed)) dac_local_buffer;

int dac_send_data(dac_conn_t *conn, struct dac_point *data, int npoints, int rate) { 
	int res;
	const struct dac_status *st = &conn->resp.dac_status;

	/* Write the header */

	if (st->playback_state == 0) {
		flog("L: Sending prepare command...\n");
		char c = 'p';
		if ((res = dac_sendall(conn, &c, sizeof(c))) < 0)
			return res;

		/* Read ACK */
		conn->pending_meta_acks++;

		/* Block here until all ACKs received... */
		while (conn->pending_meta_acks)
			dac_get_acks(conn, 1500);

		flog("L: prepare ACKed\n");
	}

	if (st->buffer_fullness > 1600 && st->playback_state == 1 && !conn->begin_sent) {
		flog("L: Sending begin command...\n");

		struct begin_command b;
		b.command = 'b';
		b.point_rate = rate;
		b.low_water_mark = 0;
		if ((res = dac_sendall(conn, &b, sizeof(b))) < 0)
			return res;

		conn->begin_sent = 1;
		conn->pending_meta_acks++;
	}

	dac_local_buffer.queue.command = 'q';
	dac_local_buffer.queue.point_rate = rate;

	dac_local_buffer.header.command = 'd';
	dac_local_buffer.header.npoints = npoints;

	memcpy(&dac_local_buffer.data[0], data,
		npoints * sizeof(struct dac_point));

	dac_local_buffer.data[0].control |= DAC_CTRL_RATE_CHANGE;

	ct_assert(sizeof(dac_local_buffer) == 18008);

	/* Write the data */
	if ((res = dac_sendall(conn, &dac_local_buffer,
		8 + npoints * sizeof(struct dac_point))) < 0)
		return res;

	/* Expect two ACKs */
	conn->pending_meta_acks++;
	conn->ackbuf[conn->ackbuf_prod] = npoints;
	conn->ackbuf_prod = (conn->ackbuf_prod + 1) % MAX_LATE_ACKS;
	conn->unacked_points += npoints;

	if ((res = dac_get_acks(conn, 0)) < 0)
		return res;

	return npoints;
}

int dac_get_acks(dac_conn_t *conn, int wait) {
	/* Read any ACKs we are owed */
	while (conn->pending_meta_acks
	       || (conn->ackbuf_prod != conn->ackbuf_cons)) {
		int res;
		if (wait_for_activity(conn->sock, wait)) { 
			if ((res = dac_read_resp(conn, 1)) < 0)
				return res;
			if ((res = check_data_response(conn)) < 0)
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
