/* j4cDAC abstract generator
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "render.h"

#define MAX_FIELDS	5

void dump_state(char *out, int len) {
	oscillator_t * const * oscp = oscillators;
	while (*oscp) {
		oscillator_t *osc = *oscp;
		int seclen;

		if (osc->slave_multiplier >= 0) {
			int mul = get_mul(osc->slave_multiplier);
			seclen = snprintf(out, len, "%s:%d:%d ",
				osc->name,
				osc->slave_multiplier,
				(osc->pos - (osc_master.pos * mul))
			);
		} else {
			seclen = snprintf(out, len, "%s:%d ",
				osc->name,
				osc->freq
			);
		}

		if (seclen >= len) return;
		len -= seclen;
		out += seclen;

		oscp++;
	}

	param_t * const *param = params;
	while (*param) {
		param_t *p = *param;
		int seclen = snprintf(out, len, "%s:%d ", p->name, p->value);
		if (seclen >= len) return;
		len -= seclen;
		out += seclen;
		param++;
	}
}		

static int xatoi(const char *c) {
	int n = 0;
	int m = 1;
	if (*c == '-') {
		c++;
		m = -1;
	}
	while (*c >= '0' && *c <= '9') {
		n *= 10;
		n += (*c++ - '0');
	}

	return n * m;
}

static void parse_update(char **argv) {
	if (!argv[0] || !argv[1]) return;

	oscillator_t *osc = get_oscillator(argv[0]);
	if (!osc) {
		param_t *param = get_param(argv[0]);
		if (!param) return;
		param->value = xatoi(argv[1]);
		return;
	}

	if (argv[2]) {
		osc->slave_multiplier = xatoi(argv[1]);
		int mul = get_mul(osc->slave_multiplier);
		fixed f = xatoi(argv[2]);
		osc->freq = osc_master.freq * mul / MUL_DENOM;
		osc->pos = (osc_master.pos * mul) + f;
	} else {
		osc->freq = xatoi(argv[1]);
		osc->slave_multiplier = -1;
	}
}

void abs_parse_line(char *line) {
	char *end = line + strlen(line);

	while (end != line && (end[-1] == '\r' || end[-1] == '\n'))
		(end--)[-1] = '\0';

	char *ap;

	/* Split out each space-delimited specifier in the input line */
	while ((ap = strsep(&line, " "))) {
		char **avp, *argv[MAX_FIELDS + 1];
		avp = argv;

		/* Split the line into colon-delimited fields */
		while ((*avp = strsep(&ap, ":"))) {
			if (**avp == '\0')
				continue;
			if (++avp >= &argv[MAX_FIELDS])
				break;
		}

		*avp = NULL;

		parse_update(argv);
	}
}
