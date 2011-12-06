/* j4cDAC file playback control
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

#include <dac.h>
#include <file_player.h>
#include <lightengine.h>
#include <playback.h>
#include <serial.h>
#include <tables.h>
#include <string.h>

int ilda_open(const char * fname);

int ilda_read_points(int max_points, packed_point_t *p);
void ilda_reset_file(void);

/* playback_refill
 *
 * If we're playing a file from the SD card, read some points from it
 * and refill the internal buffer.
 */
static void playback_refill(void) {
	int i;

	/* This function gets called on every main loop, but is only needed
	 * in file playback mode... */
	if (playback_src != SRC_ILDAPLAYER)
		return;

	/* How much data do we have room for? */
	int dlen = dac_request();
	packed_point_t *ptr = dac_request_addr();

	/* Have we underflowed? */
	if (dlen < 0) {
		if (le_get_state() != LIGHTENGINE_READY)
			return;

		outputf("*U*");
		dac_prepare();
		return;
	}

	/* If we don't have any more room... */
	if (dlen == 0) {
		if (dac_get_state() == DAC_PREPARED)
			dac_start();
		return;
	}

	if (!(playback_source_flags & ILDA_PLAYER_PLAYING))
		return;

/*
	if (dlen > 50)
		outputf("[!] %d", dlen);
*/

	/* Read some points from the file. */
	i = ilda_read_points(dlen, ptr);

	if (i < 0) {
		outputf("err: %d", i);
		playback_source_flags &= ~ILDA_PLAYER_PLAYING;
	} else if (i == 0) {
		ilda_reset_file();

		if (playback_source_flags & ILDA_PLAYER_REPEAT) {
			outputf("rep");
		} else {
			outputf("done");

			/* If the whole file didn't fit in the
			 * buffer, we may have to start it now. */
			dlen = 0;

			playback_source_flags &= ~ILDA_PLAYER_PLAYING;
		}
	} else {
		dac_advance(i);
	}

	/* If the buffer is nearly full, start it up */
	if (dlen < 200 && dac_get_state() == DAC_PREPARED)
		dac_start();
}

INITIALIZER(poll, playback_refill)
