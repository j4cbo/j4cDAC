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

#include <serial.h>
#include <string.h>
#include <assert.h>
#include <attrib.h>
#include <playback.h>
#include <file_player.h>
#include <ff.h>
#include <param.h>
#include <tables.h>
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

/* autoplay_invoke_handler
 *
 * Invoke a parameter handler based on a tokenized line.
 *
 * Returns 1 if the handler was called; 0 if the line was not suitable
 * due to bad argument count.
 */
static int NOINLINE autoplay_invoke(const volatile param_handler *h, const char *path,
                           int argc, char * const * const argv) {

	int parameters_expected = param_count_required[h->type];
	if (parameters_expected != -1 && argc != parameters_expected) {
		outputf("ap: bad param count for %s: expect %d got %d",
			h->address, parameters_expected, argc);
	}

	int32_t params[3];

	/* Special-case parameter types */
	if (h->type == PARAM_TYPE_S1 && argc == 1) {
		params[0] = (int32_t)argv[0];
	} else if (h->type == PARAM_TYPE_S1I1) {
		params[0] = (int32_t)argv[0];
		params[1] = atoi(argv[1]);
	} else if (h->type <= PARAM_TYPE_IN) {
		int p;
		for (p = 0; p < argc && p < ARRAY_NELEMS(params); p++) {
			if (h->intmode == PARAM_MODE_INT)
				params[p] = atoi(argv[p]);
			else
				params[p] = strtofixed(argv[p]);
		}
	}

	switch (h->type) {
	case PARAM_TYPE_0:
		outputf("ap: %s", h->address);
		break;
	case PARAM_TYPE_I1:
		outputf("ap: %s %d", h->address, params[0]);
		break;
	case PARAM_TYPE_I2:
		outputf("ap: %s %d %d", h->address, params[0], params[1]);
		break;
	case PARAM_TYPE_I3:
		outputf("ap: %s %d %d %d", h->address, params[0], params[1], params[2]);
		break;
	case PARAM_TYPE_IN:
		outputf("ap: %s ...", h->address);
		break;
	case PARAM_TYPE_BLOB:
		// can't pass blobs from autoplay
		return 0;
	case PARAM_TYPE_S1:
		outputf("ap: %s \"%s\"", h->address, argv[0]);
		break;
	case PARAM_TYPE_S1I1:
		outputf("ap: %s \"%s\" %d", h->address, argv[0], params[1]);
		break;
	}

	return FPA_param(h, path, params, argc);
}

/* autoplay_process_line
 *
 * Process a line from the autoplay file. Each line is space-seprated:
 *    /oscpath/to/set [value [value [value]]]
 */
static int autoplay_process_line(char *line) {
	/* Tokenize line - this is straight out of the strsep man page */
	char **ap, *argv[4], *inputstring = line;
	for (ap = argv; (*ap = strsep(&inputstring, " \t")) != NULL; )
		if (**ap != '\0')
			if (++ap >= &argv[ARRAY_NELEMS(argv)])
                                   break;

	/* Make sure we got at least a path */
	int nargs = ap - argv;
	if (!nargs) return 0;
	nargs--;

	int matched = 0;
	const volatile param_handler *h;
	foreach_matching_handler(h, argv[0]) {
		matched += autoplay_invoke(h, argv[0], nargs, argv + 1);
	}

	if (matched) {
		return 0;
	} else {
		outputf("ap: unmatched \"%s\"", argv[0]);
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
