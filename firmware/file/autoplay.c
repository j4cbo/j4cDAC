/* j4cDAC autoplay code
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

#include <diskio.h>
#include <serial.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <playback.h>
#include <file_player.h>
#include <ff.h>
#include <tables.h>
#include <dac.h>
#include <stdlib.h>

#define AUTOPLAY_FILE_NAME	"autoplay.txt"
#define AUTOPLAY_LINE_MAX	40

static FIL autoplay_file;
static uint8_t autoplay_active = 0;
static uint8_t autoplay_bytes;
static char autoplay_buf[AUTOPLAY_LINE_MAX];

/* See if we have an autoplay file; if so, open it up
 */
void autoplay_init(void) {
	FRESULT res = f_open(&autoplay_file, AUTOPLAY_FILE_NAME, FA_READ);
	if (res) {
		outputf("autoplay_init: no file: %d", res);
		return;
	}

	autoplay_active = 1;
	autoplay_bytes = 0;

	f_lseek(&autoplay_file, 0);
}

/* Is there a newline in the buffer?
 */
static char * autoplay_next_newline(void) {
	char * next_r = memchr(autoplay_buf, '\r', autoplay_bytes);
	char * next_n = memchr(autoplay_buf, '\n', autoplay_bytes);
	if (next_r && next_n) {
		if (next_r < next_n) return next_r;
		else return next_n;
	} else if (next_r) {
		return next_r;
	} else {
		return next_n;
	}
}

static int autoplay_process_line(char *line) {
	if (!line[0])
		return 0;

	if (!strncmp(line, "pps ", 4)) {
		int pps = atoi(line + 4);
		if (pps < 1000 || pps > 50000) {
			outputf("ap: ? \"%s\"", line);
			return -1;
		}
		outputf("ap: pps %d", pps);
		dac_set_rate(pps);
		return 0;
	} else if (!strncmp(line, "fps ", 4)) {
		int fps = atoi(line + 4);
		if (fps > 1000) {
			outputf("ap: ? \"%s\"", line);
			return -1;
		}
		outputf("ap: fps %d", fps);
		ilda_set_fps_limit(fps);
		return 0;
	} else if (!strncmp(line, "play ", 5)) {
		outputf("ap: play \"%s\"", line + 5);
		if (fplay_open(line + 5) == 0) {
			dac_prepare();
			playback_src = SRC_ILDAPLAYER;
			playback_source_flags |= ILDA_PLAYER_PLAYING;
			return 0;
		} else {
			return -1;
		}
	} else {
		outputf("ap: ? \"%s\"", line);
		return -1;
	}
}

void autoplay_poll(void) {
	if (!autoplay_active)
		return;

	/* If we're still playing whatever it is we may have been playing
	 * before, don't read anything else */
	if (playback_source_flags & ILDA_PLAYER_PLAYING)
		return;

	/* Try to maximally fill the autoplay line buffer */
	char * nl = autoplay_next_newline();
	if (!nl) {
		FRESULT res;
		unsigned int read = 0;
		outputf("reading %d", AUTOPLAY_LINE_MAX - autoplay_bytes);
		res = f_read(&autoplay_file, autoplay_buf + autoplay_bytes,
			AUTOPLAY_LINE_MAX - autoplay_bytes, &read);

		if (res != FR_OK) {
			/* Bail */
			outputf("al: read error %d");
			autoplay_active = 0;
			return;
		}

		if (!read) {
			outputf("ap: done");
			autoplay_active = 0;
			return;
		}

		autoplay_bytes += read;

		nl = autoplay_next_newline();
		if (!nl) {
			autoplay_buf[AUTOPLAY_LINE_MAX - 1] = '\0';
			outputf("ap: no eol: \"%s\"",
				autoplay_buf);
			autoplay_active = 0;
			return;
		}
	}

	/* Process the line */
	int line_length = nl - autoplay_buf;
	*nl = '\0';
	if (autoplay_process_line(autoplay_buf) < 0) {
		autoplay_active = 0;
		return;
	}

	/* Move up the buffer */
	memmove(autoplay_buf, nl + 1, AUTOPLAY_LINE_MAX - line_length + 1);

	autoplay_bytes -= line_length + 1;
}

INITIALIZER(poll, autoplay_poll)
INITIALIZER(protocol, autoplay_init)
