/* j4cDAC file-based SD shim
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

#include <sdcard.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct sdcard_config sdcard_config;
uint8_t sdcard_card_type;

static FILE * sdcard_file;

int sdcard_wait_for_ready(void) {
	return 0;
}

int sdcard_init(void) {
	sdcard_file = fopen("sdcard.img", "rw");
	if (!sdcard_file)
		return -1;

	struct stat st;
	fstat(fileno(sdcard_file), &st);

 	sdcard_config.blocksize = 512;
	sdcard_config.size = st.st_size;
	sdcard_config.sectorcnt = st.st_size / 512; 
	printf("%d blocks.\n", sdcard_config.sectorcnt);
	return 0;
}

int sdcard_read(uint8_t * buf, int sector, int count) {
	printf("<sd> reading %d from %d", count, sector);
	int res = fseek(sdcard_file, sector * 512, SEEK_SET);
	if (res < 0) {
		perror("fseek");
		return -1;
	}
	res = fread(buf, 512, count, sdcard_file);
	if (res < 0) {
		perror("fread");
		return -1;
	}
	return 0;
}

int sdcard_write(const uint8_t * buf, int sector, int count) {
	printf("<sd> writing %d to %d", count, sector);
	int res = fseek(sdcard_file, sector * 512, SEEK_SET);
	if (res < 0) {
		perror("fseek");
		return -1;
	}
	res = fwrite(buf, 512, count, sdcard_file);
	if (res < 0) {
		perror("fwrite");
		return -1;
	}
	return 0;
}

int sdcard_get_sd_status(uint8_t * buf) {
	return 0;
}
