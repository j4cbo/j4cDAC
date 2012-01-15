#include <serial.h>
#include <tables.h>
#include <osc.h>
#include <dac.h>
#include <stdlib.h>
#include <string.h>
#include <transform.h>
#include <dac_settings.h>

#define GEOM_LOCK_TOP	(1<<0)
#define GEOM_LOCK_BOT	(1<<1)
#define GEOM_LOCK_LEFT	(1<<2)
#define GEOM_LOCK_RIGHT	(1<<3)

static uint8_t geom_lock_flags = 0;

static const char * const corner_strings[] = {
	[CORNER_TL] = "/geom/tl",
	[CORNER_TR] = "/geom/tr",
	[CORNER_BL] = "/geom/bl",
	[CORNER_BR] = "/geom/br"
};

/* geom_send_corner
 *
 * Send the position of a given corner out via OSC.
 */
static void geom_send_corner(int corner) {
	osc_send_fixed2(corner_strings[corner],
		settings.transform_y[corner] * 2,
		settings.transform_x[corner] * 2);
}

/* geom_readout
 *
 * Send the current state of the geometric corrector to the controller.
 */
static void geom_readout(const char *path) {
	int i;
	for (i = 0; i < 4; i++)
		geom_send_corner(i);

	osc_send_int("/geom/locktop", geom_lock_flags & GEOM_LOCK_TOP ? 1 : 0);
	osc_send_int("/geom/lockbottom", geom_lock_flags & GEOM_LOCK_BOT ? 1 : 0);
	osc_send_int("/geom/lockleft", geom_lock_flags & GEOM_LOCK_LEFT ? 1 : 0);
	osc_send_int("/geom/lockright", geom_lock_flags & GEOM_LOCK_RIGHT ? 1 : 0);
}

static int can_set_corner(int corner, int x, int y) {
	int i;
	for (i = 0; i < 4; i++) {
		if (i == corner) continue;
		if (   (abs(settings.transform_x[i] - x) < COORD_TOO_CLOSE)
		    && (abs(settings.transform_y[i] - y) < COORD_TOO_CLOSE))
			return 0;
	}

	return 1;
}

static void geom_update(const char *path, int32_t y, int32_t x) {
	int corner;

	/* Inputs from OSC are in the range -1 to 1; scale them down. */
	x /= 2;
	y /= 2;

	/* Which corner are we moving? */
	if (!strcmp(path, "/geom/tl")) corner = CORNER_TL;
	else if (!strcmp(path, "/geom/tr")) corner = CORNER_TR;
	else if (!strcmp(path, "/geom/bl")) corner = CORNER_BL;
	else if (!strcmp(path, "/geom/br")) corner = CORNER_BR;
	else return;

	/* We may also be moving one or two other corners... */
	int other_corner_h = corner;
	if (((geom_lock_flags & GEOM_LOCK_TOP) && IS_TOP(corner)) ||
	    ((geom_lock_flags & GEOM_LOCK_BOT) && IS_BOTTOM(corner))) {
		other_corner_h = CORNER_FLIP_V(corner);
		if (!can_set_corner(other_corner_h,
		     settings.transform_x[other_corner_h], y)) return;
	}

	int other_corner_v = corner;
	if (((geom_lock_flags & GEOM_LOCK_LEFT) && IS_LEFT(corner)) ||
	    ((geom_lock_flags & GEOM_LOCK_RIGHT) && IS_RIGHT(corner))) {
		other_corner_v = CORNER_FLIP_H(corner);
		if (!can_set_corner(other_corner_v, x,
		     settings.transform_y[other_corner_v])) return;
	}

	if (!can_set_corner(corner, x, y)) return;

	/* OK, we are clear to go. */
	settings.transform_x[corner] = x;
	settings.transform_y[corner] = y;
	settings.transform_x[other_corner_v] = x;
	settings.transform_y[other_corner_h] = y;

	/* Update the other corners if needed. */
	if (other_corner_v != corner)
		geom_send_corner(other_corner_v);

	if (other_corner_h != corner)
		geom_send_corner(other_corner_h);

	update_transform();
}

static void geom_update_lock(const char *path, int32_t v) {
	int mask;
	if (!strcmp(path, "/geom/locktop")) mask = GEOM_LOCK_TOP;
	else if (!strcmp(path, "/geom/lockbottom")) mask = GEOM_LOCK_BOT;
	else if (!strcmp(path, "/geom/lockleft")) mask = GEOM_LOCK_LEFT;
	else if (!strcmp(path, "/geom/lockright")) mask = GEOM_LOCK_RIGHT;
	else return;

	if (v)
		geom_lock_flags |= mask;
	else
		geom_lock_flags &= ~mask;
}

static int geom_sz, geom_offset_x, geom_offset_y;

static fixed coord_clamp(fixed v) {
	if (v > COORD_MAX) return COORD_MAX;
	if (v < -COORD_MAX) return -COORD_MAX;
	return v;
}

static void geom_set_sz_offset(void) {
	int sz = geom_sz / 2;
	int off_x = geom_offset_x / 2;
	int off_y = geom_offset_y / 2;
	settings.transform_x[CORNER_TL] = coord_clamp(-sz + off_x);
	settings.transform_x[CORNER_TR] = coord_clamp(sz + off_x);
	settings.transform_x[CORNER_BL] = coord_clamp(-sz + off_x);
	settings.transform_x[CORNER_BR] = coord_clamp(sz + off_x);
	settings.transform_y[CORNER_TL] = coord_clamp(sz + off_y);
	settings.transform_y[CORNER_TR] = coord_clamp(sz + off_y);
	settings.transform_y[CORNER_BL] = coord_clamp(-sz + off_y);
	settings.transform_y[CORNER_BR] = coord_clamp(-sz + off_y);
	update_transform();
}

static void geom_set_size(const char *path, int32_t sz) {
	geom_sz = sz;
	geom_set_sz_offset();
}

static void geom_set_offset(const char *path, int32_t offset_x, int32_t offset_y) {
	geom_offset_x = offset_x;
	geom_offset_y = offset_y;
	geom_set_sz_offset();
}

static void geom_set_rdelay(const char *path, int32_t delay) {
	delay_line_set_delay(0, delay);
}

static void geom_set_gdelay(const char *path, int32_t delay) {
	delay_line_set_delay(1, delay);
}

static void geom_set_bdelay(const char *path, int32_t delay) {
	delay_line_set_delay(2, delay);
}

TABLE_ITEMS(param_handler, correction_param_updaters,
	{ "/geom/tl", PARAM_TYPE_I2, { .f2 = geom_update }, PARAM_MODE_FIXED, FIXED(-1), FIXED(1) },
	{ "/geom/tr", PARAM_TYPE_I2, { .f2 = geom_update }, PARAM_MODE_FIXED, FIXED(-1), FIXED(1) },
	{ "/geom/bl", PARAM_TYPE_I2, { .f2 = geom_update }, PARAM_MODE_FIXED, FIXED(-1), FIXED(1) },
	{ "/geom/br", PARAM_TYPE_I2, { .f2 = geom_update }, PARAM_MODE_FIXED, FIXED(-1), FIXED(1) },
	{ "/geom", PARAM_TYPE_0, { .f0 = geom_readout } },
	{ "/geom/locktop", PARAM_TYPE_I1, { .f1 = geom_update_lock } },
	{ "/geom/lockbottom", PARAM_TYPE_I1, { .f1 = geom_update_lock } },
	{ "/geom/lockleft", PARAM_TYPE_I1, { .f1 = geom_update_lock } },
	{ "/geom/lockright", PARAM_TYPE_I1, { .f1 = geom_update_lock } },
	{ "/geom/size", PARAM_TYPE_I1, { .f1 = geom_set_size }, PARAM_MODE_FIXED, FIXED(0.01), FIXED(1) },
	{ "/geom/offset", PARAM_TYPE_I2, { .f2 = geom_set_offset }, PARAM_MODE_FIXED, FIXED(-1), FIXED(1) },
	{ "/geom/rdelay", PARAM_TYPE_I1, { .f1 = geom_set_rdelay }, PARAM_MODE_INT, 0, 15 },
	{ "/geom/gdelay", PARAM_TYPE_I1, { .f1 = geom_set_gdelay }, PARAM_MODE_INT, 0, 15 },
	{ "/geom/bdelay", PARAM_TYPE_I1, { .f1 = geom_set_bdelay }, PARAM_MODE_INT, 0, 15 },
)
