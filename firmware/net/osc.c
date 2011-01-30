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

void handle_acc(const char *path, int x, int y, int z) {
}

void handle_fader(const char *path, int v) {
	outputf("%d", v);
}

TABLE(osc_handler, osc_handler)

TABLE_ITEMS(osc_handler, default_handlers,
	{ "/accxyz", 3, { .f3 = handle_acc }, { 1000000, 1000000, 1000000 } },
	{ "/1/fader1", 1, { .f1 = handle_fader }, { 1000000 } }
)

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

	int i;
	int nmatched = 0;
	for (i = 0; i < TABLE_LENGTH(osc_handler); i++) {
		const struct osc_handler *h = &osc_handler_table[i];

		if (strcmp(h->address, address))
			continue;

		nmatched++;

		/* If this has no function to call, skip it. */
		if (!h->dummy)
			continue;

		/* Parse in all the parameters. */
		int p = 0;
		int params[3] = { -1, -1, -1 };
		union float_int f;
		while (*type && p < ARRAY_NELEMS(params)) {
			switch (*type) {
			case 'f':
				f.i = ntohl(*idata);
				params[p] = f.f * h->scalefactor[p];
				idata++;
				break;
			case 'i':
				params[p] = ntohl(*idata);
				idata++;
				break;
			
			case 'T':
				params[p] = 1;
				break;

			case 'F':
				params[p] = 0;
				break;

			case 'N':
				params[p] = -1;
				break;

			default:
				/* Unrecognized type code - we must ignore
				 * this message. */
				return;
			}
			p++;
		}

		/* Now, call the appropriate function */
		switch (h->nargs) {
		case 0:
			h->f0(address);
			break;
		case 1:
			h->f1(address, params[0]);
			break;
		case 2:
			h->f2(address, params[0], params[1]);
			break;
		case 3:
			h->f3(address, params[0], params[1], params[2]);
			break;
		default:
			panic("bad nargs in osc handler list: %d i=%d",
			      i, h->nargs);
		}
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
	bytes_used += vlen;
	while (bytes_used % 4) 
		buf[bytes_used++] = '\0';

	udp_sendto(&osc_pcb, p, osc_last_source, 60001);
	pbuf_free(p);
}

INITIALIZER(protocol, osc_init)
