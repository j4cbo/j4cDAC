/* j4cDAC playback sources
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

#ifndef PLAYBACK_H
#define PLAYBACK_H

enum playback_source {
	SRC_NETWORK = 0,
	SRC_ILDAPLAYER = 1,
	SRC_SYNTH = 2
};

extern enum playback_source playback_src;
extern int playback_source_flags;

#define ILDA_PLAYER_PLAYING	0x01
#define ILDA_PLAYER_REPEAT	0x02

#endif
