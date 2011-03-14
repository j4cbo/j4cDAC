#include <stdint.h>

/* code derived from http://www.ietf.org/rfc/rfc1952.txt */

static uint32_t crc_table[256];
static int crc_table_computed = 0;

/* Make the table for a fast CRC. */
static void make_crc_table() {
	uint32_t c;
	int n, k;
	for (n = 0; n < 256; n++) {
		c = (uint32_t) n;
		for (k = 0; k < 8; k++) {
			if (c & 1) {
				c = 0xedb88320L ^ (c >> 1);
			} else {
				c = c >> 1;
			}
		}
		crc_table[n] = c;
	}
}

uint32_t crc32(uint32_t crc, uint8_t *buf, int len) {
	uint32_t c = crc ^ 0xffffffffL;
	int n;

	if (!crc_table_computed)
		make_crc_table();

	for (n = 0; n < len; n++) {
		c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}

	return c ^ 0xffffffffL;
}
