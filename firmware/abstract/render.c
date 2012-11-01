/* j4cDAC
 *
 * Copyright 2011 Jacob Potter
 * Based on SpiroDAC, by Dean Hammonds
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

#include <stdlib.h>
#include <string.h>
#include "fixpoint.h"
#include "render.h"

oscillator_t osc_master = { "master", FIXED(100), 0, -1 };
static oscillator_t osc_x = { "x", FIXED(99.8), 0, -1 };
static oscillator_t osc_y = { "y", FIXED(100), 0, -1 };
static oscillator_t osc_z = { "z", FIXED(100), 0, -1 };
static oscillator_t osc_red = { "red", FIXED(99.5), 0, -1 };
static oscillator_t osc_green = { "green", FIXED(100), 0, -1 };
static oscillator_t osc_blue = { "blue", FIXED(100.5), 0, -1 };
static oscillator_t osc_blank = { "blank", 0, 0, -1 };

static param_t x_rot = { "xrot", 0 };
static param_t y_rot = { "yrot", 0 };
static param_t mode_sel = { "mode", 0 };
static param_t rwfm = { "redwfm", 0 };
static param_t gwfm = { "greenwfm", 0 };
static param_t bwfm = { "bluewfm", 0 };
static param_t rmul = { "redmul", 65536 };
static param_t gmul = { "greenmul", 65536 };
static param_t bmul = { "bluemul", 65536 };
static param_t blankwfm = { "blankwfm", 0 };

static const int mul_coeffs[] = {
	3, 4, 6, 12, 18, 24, 36, 48, 60, 72, 84, 96, 108, 120
};

#define MUL_DENOM	12

oscillator_t * const oscillators[] = {
	&osc_master,
	&osc_x, &osc_y, &osc_z,
	&osc_red, &osc_green, &osc_blue,
	&osc_blank,
	NULL
};

param_t * const params[] = {
	&x_rot, &y_rot, &mode_sel, &blankwfm,
	&rwfm, &gwfm, &bwfm,
	&rmul, &gmul, &bmul, NULL
};

fixed fix_sine(uint32_t phase);

int get_mul(int index) {
	if (index < 0) return 1;
	if (index >= (sizeof(mul_coeffs) / sizeof(mul_coeffs[0]))) return 1;
	return mul_coeffs[index];
}

oscillator_t *get_oscillator(const char *name) {
	int i;
	for (i = 0; oscillators[i]; i++) {
		if (!strcmp(oscillators[i]->name, name))
			return oscillators[i];
	}
	return NULL;
}

param_t *get_param(const char *name) {
	int i;
	for (i = 0; params[i]; i++) {
		if (!strcmp(params[i]->name, name))
			return params[i];
	}
	return NULL;
}

static fixed render_oscillator(oscillator_t *osc, int mode) {
	if (!osc->freq)
		return 65535;

	switch (mode) {
	case 0:
		return (fix_sine(osc->pos - (1<<30)) >> 1) + 32768;
	case 1:
		if (osc->pos < (1<<31))
			return osc->pos >> 15;
		else
			return (-osc->pos) >> 15;
	case 2:
		return osc->pos >> 16;
	case 3:
		return (-osc->pos) >> 16;
	case 4:
		if (osc->pos < ((1U << 31) / 5)) return 65535;
		else return 0;
	case 5:
		if (osc->pos < ((1U << 30) * 1)) return 65535;
		else return 0;
	case 6:
		if (osc->pos < ((1U << 30) * 2)) return 65535;
		else return 0;
	case 7:
		if (osc->pos < ((1U << 30) * 3)) return 65535;
		else return 0;
	case 8:
		if (osc->pos < (((1U << 31) / 5) * 9)) return 65535;
		else return 0;
	default:
		return 0;
	}
}

void get_next_point(dac_point_t *p) {
	fixed blank;

	blank = render_oscillator(&osc_blank, blankwfm.value);
	p->r = fix_mul(render_oscillator(&osc_red, rwfm.value), fix_mul(blank, rmul.value));
	p->g = fix_mul(render_oscillator(&osc_green, gwfm.value), fix_mul(blank, gmul.value));
	p->b = fix_mul(render_oscillator(&osc_blue, bwfm.value), fix_mul(blank, bmul.value));

	fixed xv, yv, zv, r;

	if (mode_sel.value < 1) mode_sel.value = 1;

	switch (mode_sel.value) {
	case 1:
		xv = fix_sine(osc_x.pos);
		yv = fix_sine(osc_y.pos);
		zv = fix_sine(osc_z.pos);
		break;
	case 2:
		r = fix_sine(osc_z.pos);
		xv = fix_mul(fix_sine(osc_x.pos), r);
		yv = fix_mul(fix_sine(osc_y.pos), r);
		zv = 0;
		break;
	case 3:
		r = (fix_sine(osc_x.pos + osc_y.pos) + FIXED(1)) / 2;
		xv = fix_mul(fix_sine(osc_x.pos), r);
		yv = fix_mul(fix_sine(osc_y.pos), r);
		zv = fix_sine(osc_z.pos);
		break;
	case 4:
		xv = fix_mul(fix_sine(osc_x.pos), fix_sine(osc_x.pos + osc_x.pos / 5));
		yv = fix_sine(osc_y.pos);
		zv = fix_sine(osc_z.pos);
		break;
	case 5:
		r = (fix_sine(osc_y.pos) + FIXED(1)) / 2;
		xv = fix_mul(fix_sine(osc_x.pos), r);
		yv = fix_mul(fix_sine(osc_y.pos), r);
		zv = fix_sine(osc_z.pos);
		break;
	default:
		mode_sel.value = 6;
		r = (fix_sine(osc_y.pos + (UINT32_MAX / 4)) + FIXED(3)) / 3;
		xv = fix_mul(fix_sine(osc_x.pos), r);
		yv = fix_mul(fix_sine(osc_y.pos), r);
		zv = fix_sine(osc_z.pos) / 3;
		break;
	}

	fixed sin_ax = fix_sine(x_rot.value);
	fixed sin_ay = fix_sine(y_rot.value);
	fixed cos_ax = fix_sine(x_rot.value + UINT32_MAX/4);
	fixed cos_ay = fix_sine(y_rot.value + UINT32_MAX/4);

	fixed r_x = fix_mul(xv, cos_ay) + fix_mul((fix_mul(zv, cos_ax) + fix_mul(yv, sin_ax)), sin_ay);
	p->x = r_x >> 2;

	fixed r_y = fix_mul(yv, cos_ax) - fix_mul(zv, sin_ax);
	p->y = r_y >> 2;

	/* Advance all the oscillators */
	int i;
	for (i = 0; oscillators[i]; i++) {
		oscillator_t *osc = oscillators[i];
		if (i) {
			osc->pos += (fix_mul(osc->freq, UINT32_MAX / PPS));
		} else {
			osc->pos += (fix_mul(osc->freq, UINT32_MAX / PPS)) / MUL_DENOM;
		}
	}
}
