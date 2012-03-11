#include <stdint.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <dac.h>
#include <lightengine.h>
#include <string.h>
#include <assert.h>
#include <broadcast.h>
#include <protocol.h>
#include <tables.h>
#include <skub.h>

#define RV __attribute__((warn_unused_result))

extern const char build[];

uint8_t ps_plugin[PLUGIN_SIZE] __attribute__((aligned(16)));
static int ps_plugin_enabled;

static int __attribute__((noinline)) invoke_plugin(void * dest, void * src) {
	return ((int (*)(void*, void*))(ps_plugin + 1))(dest, src);
}

enum fsm_result {
	NEEDMORE,
	OK,
	FAIL
};

#define PS_DEFERRED_ACK_MAX	24

/* Set PS_BUFFER_SIZE to the largest contiguous amount of data that the
 * recv_fsm may need. Currently, this is 18 bytes, sizeof(struct dac_point).
 */
#define PS_BUFFER_SIZE		18

typedef struct stream_conn_t {
	int pointsleft;
	int buffered;

	struct {
		char cmd;
		char resp;
	} deferred_ack_queue[PS_DEFERRED_ACK_MAX];

	uint8_t deferred_ack_produce;
	uint8_t deferred_ack_consume;

	uint8_t buffer[PS_BUFFER_SIZE];

	enum {
		MAIN, DATA, DATA_PLUGIN, DATA_ABORTING, INSTALL
	} state;

} stream_conn_t;

#define ct_assert(e) ((void)sizeof(char[1 - 2*!(e)]))

/* close_conn
 *
 * Close the current connection, and record why.
 */
static int close_conn(struct tcp_pcb * pcb, uint16_t reason, int k) {
	outputf("close_conn: %d", reason);
	skub_free_sz(pcb->callback_arg);
	tcp_sent(pcb, NULL);
	tcp_recv(pcb, NULL);
	tcp_close(pcb);
	return k;
}


static int ps_defer_ack(stream_conn_t * s, char cmd, char resp) {
	if ((s->deferred_ack_produce + 1) % PS_DEFERRED_ACK_MAX
	     == s->deferred_ack_consume) {
		return -1;
	}

	uint8_t prod = s->deferred_ack_produce;
	s->deferred_ack_queue[prod].cmd = cmd;
	s->deferred_ack_queue[prod].resp = resp;
	s->deferred_ack_produce = (prod + 1) % PS_DEFERRED_ACK_MAX;

	return 0;
}

static int ps_send_deferred_acks(struct tcp_pcb *pcb) {
	stream_conn_t * s = pcb->callback_arg;

	int num = (s->deferred_ack_produce + PS_DEFERRED_ACK_MAX
	           - s->deferred_ack_consume) % PS_DEFERRED_ACK_MAX;
	if (num) {
		outputf("%d deferred acks\n", num);
	}

	int count = 0;
	int consume = s->deferred_ack_consume;
	err_t err = 0;

	while (consume != s->deferred_ack_produce) {
		struct dac_response response;
		response.response = s->deferred_ack_queue[consume].resp;
		response.command = s->deferred_ack_queue[consume].cmd;
		fill_status(&response.dac_status);

		err = tcp_write(pcb, &response, sizeof(response),
			TCP_WRITE_FLAG_COPY);

		if (err < 0)
			break;

		count++;
		consume = (consume + 1) % PS_DEFERRED_ACK_MAX;
	}

	s->deferred_ack_consume = consume;

	if (err == ERR_MEM)
		err = 0;

	if (num)
		outputf("sent %d deferred acks, err %d", count, err);

	return err;
}

/* send_resp
 *
 * Send a response back to the user.
 *
 * If the send is successful, this will return its 'length' parameter. If
 * not, it will close the connection, and then return -1. This is intended
 * for use by recv_fsm, which can tail-call send_resp with the number of
 * bytes that were consumed from the input. If the send succeeds, then that
 * value is returned; otherwise, the error is propagated up.
 */
static int RV send_resp(struct tcp_pcb *pcb, char resp, char cmd, int len) {
	struct dac_response response;
	response.response = resp;
	response.command = cmd;
	fill_status(&response.dac_status);

	err_t err = tcp_write(pcb, &response, sizeof(response),
			      TCP_WRITE_FLAG_COPY);

	if (err == ERR_MEM) {
		if (ps_defer_ack(pcb->callback_arg, cmd, resp) < 0) {
			outputf("!!! DROPPING ACK !!!");
		} else {
			outputf("deferring ACK");
		}
	} else if (err != ERR_OK) {
		outputf("tcp_write returned %d", err);
		return close_conn(pcb, CONNCLOSED_SENDFAIL, len);
	}

	err = tcp_output(pcb);

	if (err != ERR_OK) {
		outputf("tcp_output returned %d", err);
		return close_conn(pcb, CONNCLOSED_SENDFAIL, len);
	}

	return len;
}

/* send_version_resp
 *
 * Send our version string back to the host.
 *
 * This looks a lot like send_resp, but with a version string instead; also,
 * we can't defer a version response like we can an ACK, so any error in
 * send is fatal. (This is OK, since version queries don't happen under
 * heavy load.)
 *
 * This does the same CPS-style return as send_resp.
 */
static int RV send_version_resp(struct tcp_pcb *pcb, int len) {
	char buf[32];
	memset(buf, 0, sizeof(buf));
	strncpy(buf, build, sizeof(buf) - 1);

	err_t err = tcp_write(pcb, buf, sizeof(buf), TCP_WRITE_FLAG_COPY);

	/* We can't defer a version response... */
	if (err != ERR_OK) {
		outputf("tcp_write returned %d", err);
		return close_conn(pcb, CONNCLOSED_SENDFAIL, len);
	}

	err = tcp_output(pcb);

	if (err != ERR_OK) {
		outputf("tcp_output returned %d", err);
		return close_conn(pcb, CONNCLOSED_SENDFAIL, len);
	}

	return len;
}

/* recv_fsm
 *
 * Attempt to process some data from the buffer located at 'data'.
 *
 * If a command is succcessfully read, then this returns the number of bytes
 * consumed from the buffer. If a partial command is present in the buffer,
 * but not enough to process yet, then this will return 0; the invoking
 * function should call it later when more data is available. If an error
 * occurs such that no further data can be handled from the connection, then
 * this will call close_conn on the connection and return -1.
 */
static int recv_fsm(struct tcp_pcb *pcb, uint8_t * data, int len) {
	uint8_t cmd = *data;
	stream_conn_t *s = pcb->callback_arg;
	int npoints;

	switch (s->state) {
	case MAIN:
		switch (cmd) {
		case 'p':
			/* Prepare stream. */
			if (dac_prepare() < 0) {
				return send_resp(pcb, RESP_NAK_INVL, cmd, 1);
			} else {
				return send_resp(pcb, RESP_ACK, cmd, 1);
			}

		case 'b':
			/* Make sure we have all of this packet... */
			if (len < sizeof(struct begin_command))
				return 0;

			struct begin_command *bc = (struct begin_command *)data;

			if (bc->point_rate > DAC_MAX_POINT_RATE)
				return send_resp(pcb, RESP_NAK_INVL, cmd, 1);

// XXX			set_low_water_mark(bc->low_water_mark);
			dac_set_rate(bc->point_rate);
			dac_start();

			return send_resp(pcb, RESP_ACK, cmd,
					 sizeof(struct begin_command));

		case 'u':
			/* Update and Begin use the same packet format */
			if (len < sizeof(struct begin_command))
				return 0;

			struct begin_command *uc = (struct begin_command *)data;

			if (uc->point_rate > DAC_MAX_POINT_RATE)
				return send_resp(pcb, RESP_NAK_INVL, cmd, 1);

			dac_set_rate(uc->point_rate);
// XXX			set_low_water_mark(uc->low_water_mark);

			return send_resp(pcb, RESP_ACK, cmd, 
					 sizeof(struct begin_command));

		case 'q':
			if (len < sizeof(struct queue_command))
				return 0;

			struct queue_command *qc = (struct queue_command *)data;

			if (qc->point_rate > DAC_MAX_POINT_RATE)
				return send_resp(pcb, RESP_NAK_INVL, cmd, 1);

			dac_rate_queue(qc->point_rate);

			return send_resp(pcb, RESP_ACK, cmd, 
					 sizeof(struct queue_command));

		case 'd':
		case 'D':
			/* Data: switch into the DATA state to start reading
			 * points into the buffer. */
			if (len < sizeof(struct data_command))
				return 0;

			struct data_command *h = (struct data_command *) data;

			if (h->npoints) {
				if (cmd == 'd') s->state = DATA;
				else s->state = DATA_PLUGIN;
				s->pointsleft = h->npoints;

				/* We'll send a response once we've read all
				 * of the data. */
				return sizeof(struct data_command);
			} else {
				/* 0-length data packets are legit. */
				return send_resp(pcb, RESP_ACK, cmd,
					sizeof(struct data_command));
			}

		case 's':
			/* Stop */
			if (dac_get_state() == DAC_IDLE) {
				return send_resp(pcb, RESP_NAK_INVL, cmd, 1);
			} else {
				dac_stop(0);
				return send_resp(pcb, RESP_ACK, cmd, 1);
			}

		case 0:
		case 0xFF:
			/* Emergency-stop. */
			le_estop(ESTOP_PACKET);
			return send_resp(pcb, RESP_ACK, cmd, 1);

		case 'c':
			/* Clear e-stop. */
			le_estop_clear(ESTOP_CLEAR_ALL);
			if (le_get_state() == LIGHTENGINE_READY)
				return send_resp(pcb, RESP_ACK, cmd, 1);
			else
				return send_resp(pcb, RESP_NAK_ESTOP, cmd, 1);

		case '?':
			/* Ping */
			return send_resp(pcb, RESP_ACK, cmd, 1);

		case 'I':
			/* Install plugin */
			s->state = INSTALL;
			s->pointsleft = sizeof(ps_plugin);
			return 1;

		case 'P':
			/* Pass data to plugin */
			if (!ps_plugin_enabled)
				return send_resp(pcb, RESP_NAK_INVL, cmd, 17);
			if (len < 17) return 0;
			return send_resp(pcb, invoke_plugin(data + 1, NULL), cmd, 17);

		case 'v':
			/* Check version */
			return send_version_resp(pcb, 1);

		default:
			outputf("unknown cmd 0x%02x", cmd);
			return close_conn(pcb, CONNCLOSED_UNKNOWNCMD, -1);
		}

		return -1;

	case INSTALL:
		/* How many bytes? */
		npoints = len;
		if (npoints > s->pointsleft) npoints = s->pointsleft;

		/* Copy in the data and move up */
		memcpy(ps_plugin + sizeof(ps_plugin) - s->pointsleft,
		       data, npoints);
		s->pointsleft -= npoints;

		/* Still some left? */
		if (s->pointsleft)
			return npoints;

		s->state = MAIN;

		/* First byte must be nonzero */
		if (!ps_plugin[0]) {
			ps_plugin_enabled = 0;
			outputf("bad plugin");
			return send_resp(pcb, RESP_NAK_INVL, 'I',
			                 npoints);
		}

		/* Initialize */
		outputf("invoking plugin...");
		int ret = invoke_plugin(NULL, NULL);
		outputf("plugin returned %d", ret);

		if (ret) {
			/* Fail! */
			ps_plugin_enabled = 0;
			return send_resp(pcb, RESP_NAK_INVL, 'I',
			                 npoints);
		} else {
			ps_plugin_enabled = 1;
			return send_resp(pcb, RESP_ACK, 'I', npoints);
		}

		break;

	case DATA:
	case DATA_PLUGIN:
		ASSERT_NOT_EQUAL(s->pointsleft, 0);

		/* We can only write a complete point at a time. */
		if (len < sizeof(struct dac_point))
			return 0;

		/* How many bytes of data is it our business to write? */
		npoints = len / sizeof(struct dac_point);
		if (npoints > s->pointsleft)
			npoints = s->pointsleft;

		if (s->state == DATA_PLUGIN && !ps_plugin_enabled) {
			s->state = DATA_ABORTING;
			goto handle_aborted_data;
		}

		/* How much do we have room for now? Note that dac_prepare
		 * is a ring buffer, so it's OK if it returns less than the
		 * number of points we have ready for it. We'll just have
		 * the FSM invoke us again. */
		int nready = dac_request();
		packed_point_t *addr = dac_request_addr();

		/* On the other hand, if the DAC isn't ready for *any* data,
		 * then ignore the rest of this write command and NAK when
		 * it's over. The FSM will take care of us... */
		if (nready <= 0) {
			if (nready == 0) {
				outputf("overflow: wanted to write %d", npoints);
			} else {
				outputf("underflow: pl %d np %d r %d", s->pointsleft, npoints, nready);
			}
			s->state = DATA_ABORTING;

			/* Danger: goto. This could probably be structured
			 * better... */
			goto handle_aborted_data;
		}

		if (npoints > nready)
			npoints = nready;

		dac_point_t *pdata = (dac_point_t *)data;

		int i;
		for (i = 0; i < npoints; i++) {
			if (s->state == DATA) {
				dac_pack_point(addr + i, pdata + i);
			} else {
				invoke_plugin(addr + i, pdata + i);
			}

		}

		/* Let the DAC know we've given it more data */
		dac_advance(npoints);

		s->pointsleft -= npoints;

		if (!s->pointsleft) {
			char resp = (s->state == DATA) ? 'd' : 'D';
			s->state = MAIN;
			return send_resp(pcb, RESP_ACK, resp,
					 npoints * sizeof(struct dac_point));
		} else {
			return (npoints * sizeof(struct dac_point));
		}

	case DATA_ABORTING:
		ASSERT_NOT_EQUAL(s->pointsleft, 0);

		/* We can only consume a complete point at a time. */
		if (len < sizeof(struct dac_point))
			return 0;

		/* How many points do we have? */
		npoints = len / sizeof(struct dac_point);
		if (npoints > s->pointsleft)
			npoints = s->pointsleft;

handle_aborted_data:
		s->pointsleft -= npoints;

		if (!s->pointsleft) {
			s->state = MAIN;
			return send_resp(pcb, RESP_NAK_INVL, 'd',
					 npoints * sizeof(struct dac_point));
		} else {
			return (npoints * sizeof(struct dac_point));
		}

	default:
		break;
	}

	panic("invalid state in recv_dfa");
	return -1;
}

err_t process_packet_FPV_tcp_recv(struct tcp_pcb * pcb, struct pbuf * pbuf,
		     err_t err) {
	struct pbuf * p = pbuf;
	struct stream_conn_t * s = pcb->callback_arg;

	if (p == NULL) {
		return close_conn(pcb, CONNCLOSED_USER, ERR_OK);
	}

	while (p) {
		int data_left = p->len;
		uint8_t *data_ptr = p->payload;
		int fsar = 0;

		if (s->buffered) {
			/* There's some leftover data from the last pbuf that
			 * we processed, so copy it in first. */
			int more = PS_BUFFER_SIZE - s->buffered;
			if (more > data_left)
				more = data_left;

			ASSERT_NOT_EQUAL(more, 0);

			memcpy(s->buffer + s->buffered, data_ptr, more);

			fsar = recv_fsm(pcb, s->buffer, s->buffered + more);

			/* We still may not yet have enough. This should only
			 * happen becuase we're out of data in this pbuf, not
			 * because we couldn't fit it all in the buffer. */
			if (fsar == 0) {
				ASSERT_EQUAL(data_left, more);
				data_ptr += more;
				data_left -= more;
				s->buffered += more;
			} else if (fsar < 0) {
				break;
			} else {
				/* Now, depending on the command, we may not
				 * have needed *all* PS_BUFFER_SIZE bytes. The
				 * *next* command, however, should start in
				 * the pbuf - if not, something's wrong, since
				 * we should have processed the previous
				 * command before now. */
				ASSERT(fsar > s->buffered);

				/* Consume only what we actually used from the
				 * buffer, so that the next recv_fsm call can
				 * point directly at the buffer. */
				data_ptr += (fsar - s->buffered);
				data_left -= (fsar - s->buffered);
				s->buffered = 0;
			}
		}

		/* At this point, there should be no more buffered data. */
		if (data_left)
			ASSERT_EQUAL(s->buffered, 0);

		/* Now that we've dealt with playing out anything that was
		 * buffered with a previous pbuf, let's deal with this one. */
		while (data_left) {
			fsar = recv_fsm(pcb, data_ptr, data_left);

			if (fsar == 0) {
				/* There isn't enough. */
				ASSERT(data_left < PS_BUFFER_SIZE);
				memcpy(s->buffer, data_ptr, data_left);
				s->buffered = data_left;
				data_left = 0;
			} else if (fsar < 0) {
				break;
			} else {
				data_ptr += fsar;
				data_left -= fsar;
			}
		}

		if (fsar < 0)
			break;

		/* Move on to the next pbuf. */
		p = p->next;
	}

	ps_send_deferred_acks(pcb);

	/* Tell lwIP we're done with this packet. */
	tcp_recved(pcb, pbuf->tot_len);
	pbuf_free(pbuf);

	return ERR_OK;
}

static err_t ps_accept_FPV_tcp_accept(void *arg, struct tcp_pcb *pcb, err_t err) {

	/* From inspection of the lwip source (core/tcp_in.c:639), 'err'
	 * will only ever be ERR_OK, so we can ignore it here.
	 */
	LWIP_UNUSED_ARG(err);

	stream_conn_t * s = skub_alloc_sz(sizeof(stream_conn_t));
	if (!s) {
		return ERR_MEM;
	}
	memset(s, 0, sizeof(*s));

	outputf("conn");

	/* Send a hello packet. */
	if (send_resp(pcb, RESP_ACK, '?', 0) < 0)
		return ERR_MEM;

	/* Call process_packet whenever we get data. */
	tcp_recv(pcb, process_packet_FPV_tcp_recv);
	pcb->callback_arg = s;

	return ERR_OK;
}

void ps_init(void) {
	struct tcp_pcb *pcb;

	pcb = tcp_new();
	tcp_bind(pcb, IP_ADDR_ANY, 7765);
	pcb = tcp_listen(pcb);
	tcp_accept(pcb, ps_accept_FPV_tcp_accept);
}

INITIALIZER(protocol, ps_init)
