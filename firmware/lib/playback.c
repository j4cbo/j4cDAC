/* j4cDAC - playback source control
 *
 * Copyright 2010, 2011 Jacob Potter
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

#include <playback.h>
#include <dac.h>
#include <hardware.h>

enum playback_source playback_src;
int playback_source_flags;

/* playback_set_src
 *
 * Change the playback source. This will be refused if the current source
 * is an active network stream.
 */
int playback_set_src(enum playback_source new_src) {
	/* Can't switch away from network while playing. */
	if (playback_src == SRC_NETWORK && new_src != SRC_NETWORK
	    && dac_get_state() != DAC_IDLE) {
		return -1;
	}

	/* Stop the DAC and set playback_src_flags to 0, which will prevent
	 * the abstract generator and ILDA player from producing output. */
	dac_stop(DAC_FLAG_STOP_SRCSWITCH);
	playback_source_flags = 0;

	return 0;
}
