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

#include <string.h>
#include <stdint.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <attrib.h>
#include <assert.h>
#include <osc.h>
#include <tables.h>

static struct ip_addr *osc_last_source;

union float_int {
	float f;
	uint32_t i;
};

void handle_acc_FPV_param(const char *path, int32_t x, int32_t y, int32_t z) {
}

void handle_fader_FPV_param(const char *path, int32_t v) {
	outputf("%d", v);
}

TABLE(param_handler, param_handler)

TABLE_ITEMS(param_handler, default_handlers,
	{ "/accxyz", PARAM_TYPE_I3, { .f3 = handle_acc_FPV_param } },
	{ "/1/fader1", PARAM_TYPE_I3, { .f1 = handle_fader_FPV_param } }
)

/* FPA_osc
 *
 * Accessor for parameter update functions. In addition to performing the
 * indirect call, this also clamps parameters to the handler's range.
 */
int FPA_param(const volatile param_handler *h, const char *addr, int32_t *p) {
	int i;

	if (h->type != PARAM_TYPE_S1 && (h->min || h->max)) {
		for (i = 0; i < h->type; i++) {
			if (p[i] > h->max) p[i] = h->max;
			if (p[i] < h->min) p[i] = h->min;
		}
	}

	switch (h->type) {
	case PARAM_TYPE_0:
		h->f0(addr);
		break;
	case PARAM_TYPE_I1:
		h->f1(addr, p[0]);
		break;
	case PARAM_TYPE_I2:
		h->f2(addr, p[0], p[1]);
		break;
	case PARAM_TYPE_I3:
		h->f3(addr, p[0], p[1], p[2]);
		break;
	case PARAM_TYPE_S1:
		h->fs(addr, (const char *)p[0]);
		break;
	default:
		panic("bad type in param def: %s %d", h->address, h->type);
	}

	return 1;
}

void osc_parse_packet(char *data, int length) {

	/* If the length isn't a multiple of 4, someting is very fishy -
	 * everything in OSC is padded to 4 bytes.
	 */
	if (length % 4)
		return;

	/* The packet should start with an OSC Address Pattern, which will
	 * be a string starting with /. If not, something's wrong.
	 */
	if (*data != '/')
		return;

	int address_len = strnlen((char *) data, length);
	char *address = data;
	int address_padded = address_len + 4 - (address_len % 4);

	data += address_padded;
	length -= address_padded;

	if (!length) { 
		/* No data after the address. WTF? */
		return;
	}

	if (*data != ',') {
		/* We require a type tag. */
		return;
	}

	int type_len = strnlen((char *) data, length);
	char *type = data + 1;
	int type_padded = type_len + 4 - (type_len % 4);

	data += type_padded;
	length -= type_padded;

	uint32_t *idata = (uint32_t *)data;

	/* Parse in all the parameters */
	int count = 0;
	int32_t raw_params[3];

	while (type[count] && count < 3) {
		char t = type[count];
		switch (t) {
		case 'i':
		case 'f':
			/* We don't know whether we should be processing
			 * as int or fixed until we've found the handler,
			 * so to avoid loss of precision, don't do float
			 * conversion until then. */
			raw_params[count] = ntohl(*idata);
			idata++;
			break;
		case 'T':
			raw_params[count] = 1;
			break;
		case 'F':
			raw_params[count] = 0;
			break;
		case 'N':
			raw_params[count] = -1;
			break;
		default:
			/* Unrecognized type code - we must ignore
			 * this message. */
			outputf("osc: unk type '%c' (%d)", t, t);
			return;
		}
		count++;
	}

	int nmatched = 0;
	int i;
	const volatile param_handler *h;
	foreach_matching_handler(h, address) {
		int32_t params[3];

		switch (h->type) {
		case PARAM_TYPE_S1:
			/* Accept this iff we got one string parameter */
			if (count != 1 || type[0] != 's') continue;
			params[0] = raw_params[0];
			break;

		case PARAM_TYPE_0:
		case PARAM_TYPE_I1:
		case PARAM_TYPE_I2:
		case PARAM_TYPE_I3:
			/* Convert floats as needed */
			for (i = 0; i < count; i++) {
				union float_int f;
				if (type[i] == 's') continue; /* XXX want to continue outer */
				if (type[i] == 'f') {
					f.i = raw_params[i];
					if (h->intmode == PARAM_MODE_INT) {
						params[i] = f.f;
					} else {
						params[i] = FIXED(f.f);
					}
				} else {
					if (h->intmode == PARAM_MODE_INT) {
						params[i] = raw_params[i];
					} else {
						params[i] = FIXED(raw_params[i]);
					}
				}
			}

			/* Check parameter count - needs to match, with the
			 * exception that if we get a single nonzero param
			 * for something that wants 0, we accept it */
			if (h->type == PARAM_TYPE_0 && count == 1) {
				/* Swallow release events */
				if (!params[0]) {
					nmatched++;
					continue;
				}
			} else if (h->type != count) {
				continue;
			}
		}

		/* Now, call the appropriate function */
		nmatched++;
		FPA_param(h, address, params);
	}

	if (!nmatched && address_len >= 2 && address[address_len - 1] != 'z' \
	    && address[address_len - 2] != '/') {
		outputf("unk: %s", address);
	}
}

void osc_recv(void *arg, struct udp_pcb * pcb, struct pbuf * pbuf,
	       struct ip_addr *addr, u16_t port) {
	osc_last_source = addr;
	osc_parse_packet(pbuf->payload, pbuf->len);
	pbuf_free(pbuf);
}

struct udp_pcb osc_pcb;

void osc_init(void) {
	udp_new(&osc_pcb);
	udp_bind(&osc_pcb, IP_ADDR_ANY, 60000);
	udp_recv(&osc_pcb, osc_recv, 0);
}

void osc_send_int(const char *path, uint32_t value) {
	int plen = strlen(path);
	int size_needed = plen - (plen % 4) + 12;

	struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT, size_needed, PBUF_RAM);

	if (!p) {
		outputf("osc_send_int: oom");
		return;
	}

	char *buf = p->payload;

	strcpy(buf, path);
	int bytes_used = plen + 1;

	/* Pad nulls to a multiple of 4 bytes */
	while (bytes_used % 4) {
		buf[bytes_used++] = '\0';
	}

	/* Add the type tag */
	buf[bytes_used++] = ',';
	buf[bytes_used++] = 'i';

	while (bytes_used % 4) {
		buf[bytes_used++] = '\0';
	}

	*(uint32_t *)&buf[bytes_used] = htonl(value);
	bytes_used += 4;

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

void osc_send_int2(const char *path, uint32_t v1, uint32_t v2) {
	int plen = strlen(path);
	int size_needed = plen - (plen % 4) + 16;

	struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT, size_needed, PBUF_RAM);

	if (!p) {
		outputf("osc_send_int: oom");
		return;
	}

	char *buf = p->payload;

	strcpy(buf, path);
	int bytes_used = plen + 1;

	/* Pad nulls to a multiple of 4 bytes */
	while (bytes_used % 4) {
		buf[bytes_used++] = '\0';
	}

	/* Add the type tag */
	buf[bytes_used++] = ',';
	buf[bytes_used++] = 'i';
	buf[bytes_used++] = 'i';

	while (bytes_used % 4) {
		buf[bytes_used++] = '\0';
	}

	*(uint32_t *)&buf[bytes_used] = htonl(v1);
	bytes_used += 4;
	*(uint32_t *)&buf[bytes_used] = htonl(v2);
	bytes_used += 4;

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

void osc_send_string(const char *path, const char *value) {
	int plen = strlen(path);
	int vlen = strlen(value);
	int size_needed = plen - (plen % 4) + vlen - (vlen % 4) + 12;

	struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT, size_needed, PBUF_RAM);

	if (!p) {
		outputf("osc_send_int: oom");
		return;
	}

	char *buf = p->payload;

	strcpy(buf, path);
	int bytes_used = plen + 1;

	/* Pad nulls to a multiple of 4 bytes */
	while (bytes_used % 4) {
		buf[bytes_used++] = '\0';
	}

	/* Add the type tag */
	buf[bytes_used++] = ',';
	buf[bytes_used++] = 's';

	while (bytes_used % 4)
		buf[bytes_used++] = '\0';

	strcpy(buf + bytes_used, value);
	bytes_used += vlen + 1;
	while (bytes_used % 4) 
		buf[bytes_used++] = '\0';

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

INITIALIZER(protocol, osc_init)
