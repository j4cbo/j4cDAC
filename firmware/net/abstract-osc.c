/* j4cDAC abstract control via OSC
 *
 * Copyright 2012 Jacob Potter
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

#include <osc.h>
#include <stdlib.h>
#include <string.h>
#include <tables.h>
#include <stdio.h>
#include <attrib.h>
#include <dac.h>
#include <serial.h>
#include <playback.h>

void abs_parse_line(char *line);

static void abstract_conf_FPV_param(const char *path, const char *str) {

	/* Start running abstract */
	if (playback_set_src(SRC_ABSTRACT) < 0) {
		outputf("src switch err\n");
		return;
	}

	/* XXX this is no good, abs_parse_line modifies the line */
	abs_parse_line((char *)str);

	playback_source_flags |= ABSTRACT_PLAYING;

	/* go! */
	dac_set_rate(30000);
	int state = dac_get_state();
	if (state == DAC_IDLE) {
		outputf("prep %d", dac_prepare());
		outputf("start %d", dac_start());
	} else if (state == DAC_PREPARED) {
		outputf("start %d", dac_start());
	}
}

TABLE_ITEMS(param_handler, abstract_osc_handlers,
	{ "/abstract/conf", PARAM_TYPE_S1, { .fs =  abstract_conf_FPV_param } },
)
