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
	{ "/1/fader1", PARAM_TYPE_I1, { .f1 = handle_fader_FPV_param } }
)

static const int8_t param_count_required[] = {
	[PARAM_TYPE_0] = 0,
	[PARAM_TYPE_I1] = 1,
	[PARAM_TYPE_I2] = 2,
	[PARAM_TYPE_I3] = 3,
	[PARAM_TYPE_IN] = -1,
	[PARAM_TYPE_S1] = 1
};

/* FPA_osc
 *
 * Accessor for parameter update functions. In addition to performing the
 * indirect call, this also clamps parameters to the handler's range.
 */
int FPA_param(const volatile param_handler *h, const char *addr, int32_t *p, int n) {
	int i;

	/* Ensure that we have the right number of parameters */
	int8_t parameters_expected = param_count_required[h->type];
	if (parameters_expected != -1 && n != parameters_expected)
		return 0;

	if (h->type != PARAM_TYPE_S1 && (h->min || h->max)) {
		for (i = 0; i < n; i++) {
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
	case PARAM_TYPE_IN:
		h->fi(addr, p, n);
		break;
	case PARAM_TYPE_S1:
		h->fs(addr, (const char *)p[0]);
		break;
	default:
		panic("bad type in param def: %s %d", h->address, h->type);
	}

	return 1;
}

int osc_try_handler(volatile const param_handler *h, char *address, char *type, uint32_t *data, int length) ALWAYS_INLINE;
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

	/* Try matching against each parameter handler */
	int nmatched = 0;
	volatile const param_handler *h;
	foreach_matching_handler(h, address) {
		nmatched += osc_try_handler(h, address, type, (uint32_t *)data, length);
	}

	if (!nmatched && address_len >= 2 && address[address_len - 1] != 'z' \
	    && address[address_len - 2] != '/') {
		outputf("unk: %s", address);
	}
}

int osc_try_handler(volatile const param_handler *h, char *address, char *type, uint32_t *data, int length) {
	int32_t params[50];
	int i;
	union float_int f;

	/* Parse in all the parameters according to how the parameter wants them
	 * to be interpreted. */
	for (i = 0; i < ARRAY_NELEMS(params) && type[i] && length >= 4; i++) {
		char t = type[i];
		switch (t) {
		case 'i':
			if (h->intmode == PARAM_MODE_INT) {
				params[i] = ntohl(*data);
			} else {
				params[i] = FIXED(ntohl(*data));
			}
			data++;
			break;
		case 'f':
			f.i = ntohl(*data);
			if (h->intmode == PARAM_MODE_INT) {
				params[i] = f.f;
			} else {
				params[i] = FIXED(f.f);
			}
			data++;
			break;
		case 'T':
			params[i] = 1;
			break;
		case 'F':
			params[i] = 0;
			break;
		case 'N':
			params[i] = -1;
			break;
		default:
			/* Unrecognized type code - we must ignore
			 * this message. */
			outputf("osc: unk type '%c' (%d)", t, t);
			return -1;
		}
	}

	/* If we get a single parameter for something that expects none, and
	 * it's nonzero, pretend we got no parameters; if it is zero, swallow
	 * the event. This makes things like TouchOSC pushbuttons work right.
	 */
	if (h->type == PARAM_TYPE_0 && i == 1) {
		if (params[0]) i = 0;
		else return 1;
	}

	/* Finally, call FPA_param with our inputs */
	FPA_param(h, address, params, i);
	return 1;
}

void osc_recv_FPV_udp_recv(struct udp_pcb * pcb, struct pbuf * pbuf,
	       struct ip_addr *addr, u16_t port) {
	osc_last_source = addr;
	osc_parse_packet(pbuf->payload, pbuf->len);
	pbuf_free(pbuf);
}

struct udp_pcb osc_pcb;

void osc_init(void) {
	udp_new(&osc_pcb);
	udp_bind(&osc_pcb, IP_ADDR_ANY, 60000);
	udp_recv(&osc_pcb, osc_recv_FPV_udp_recv, 0);
}

static struct pbuf * osc_setup_pbuf(const char *path, char **data, int datalen) {
	int plen = strlen(path);
	int size_needed = plen - (plen % 4) + 8 + datalen;

	struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT, size_needed, PBUF_RAM);
	if (!p) {
		outputf("osc_setup_pbuf: oom");
		return NULL;
	}

	char *buf = p->payload;

	strcpy(buf, path);
	int bytes_used = plen + 1;

	/* Pad nulls to a multiple of 4 bytes */
	while (bytes_used % 4) {
		buf[bytes_used++] = '\0';
	}

	*data = buf + bytes_used;
	return p;
}
	
void osc_send_int(const char *path, uint32_t value) {
	char *data;
	struct pbuf * p = osc_setup_pbuf(path, &data, 4);
	if (!p) return;

	/* Add the type tag */
	memcpy(data, ",i\x00\x00", 4);

	*(uint32_t *)(data + 4) = htonl(value);

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

void osc_send_int2(const char *path, uint32_t v1, uint32_t v2) {
	char *data;
	struct pbuf * p = osc_setup_pbuf(path, &data, 8);
	if (!p) return;

	/* Add the type tag */
	memcpy(data, ",ii\x00", 4);

	*(uint32_t *)(data + 4) = htonl(v1);
	*(uint32_t *)(data + 8) = htonl(v2);

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

void osc_send_fixed2(const char *path, fixed v1, fixed v2) {
	char *data;
	struct pbuf * p = osc_setup_pbuf(path, &data, 8);
	if (!p) return;

	/* Add the type tag */
	memcpy(data, ",ff\x00", 4);

	union float_int f1, f2;
	f1.f = FLOAT(v1);
	f2.f = FLOAT(v2);

	*(uint32_t *)(data + 4) = htonl(f1.i);
	*(uint32_t *)(data + 8) = htonl(f2.i);

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

void osc_send_string(const char *path, const char *value) {
	int vlen = strlen(value);
	char *data;
	struct pbuf * p = osc_setup_pbuf(path, &data, vlen - (vlen % 4) + 4);
	if (!p) return;

	/* Add the type tag */
	memcpy(data, ",s\x00\x00", 4);

	strcpy(data + 4, value);
	int bytes_used = vlen + 5;
	while (bytes_used % 4) 
		data[bytes_used++] = '\0';

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

int osc_parameter_matches(const char *handler, const char *packet) {
	while (1) {
		if (*handler == '*') {
			/* Make sure this was the last field in the path */
			while (*packet)
				if (*packet++ == '/') return 0;
			return 1;
		} else if (*handler != *packet)
			return 0;
		else if (*handler == '\0')
			return 1;

		handler++;
		packet++;
	}
}	

INITIALIZER(protocol, osc_init)
