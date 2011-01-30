#include <serial.h>
#include <tables.h>
#include <osc.h>
#include <stdlib.h>
#include <string.h>
#include <transform.h>

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
	osc_send_int2(corner_strings[corner],
		transform_y[corner] / 64, transform_x[corner] / 64);
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
		if (   (abs(transform_x[i] - x) < COORD_TOO_CLOSE)
		    && (abs(transform_y[i] - y) < COORD_TOO_CLOSE)) return 0;
	}

	return 1;
}


static void geom_update(const char *path, int y, int x) {
	int corner;

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
		if (!can_set_corner(other_corner_h, transform_x[other_corner_h], y)) return;
	}

	int other_corner_v = corner;
	if (((geom_lock_flags & GEOM_LOCK_LEFT) && IS_LEFT(corner)) ||
	    ((geom_lock_flags & GEOM_LOCK_RIGHT) && IS_RIGHT(corner))) {
		other_corner_v = CORNER_FLIP_H(corner);
		if (!can_set_corner(other_corner_v, x, transform_y[other_corner_v])) return;
	}

	if (!can_set_corner(corner, x, y)) return;

	/* OK, we are clear to go. */
	transform_x[corner] = x;
	transform_y[corner] = y;
	transform_x[other_corner_v] = x;
	transform_y[other_corner_h] = y;

	/* Update the other corners if needed. */
	if (other_corner_v != corner)
		geom_send_corner(other_corner_v);

	if (other_corner_h != corner)
		geom_send_corner(other_corner_h);

	update_transform();
}

static void geom_update_lock(const char *path, int v) {
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

TABLE_ITEMS(osc_handler, correction_osc_updaters,
	{ "/geom/tl", 2, { .f2 = geom_update }, { 64, 64 } },
	{ "/geom/tr", 2, { .f2 = geom_update }, { 64, 64 } },
	{ "/geom/bl", 2, { .f2 = geom_update }, { 64, 64 } },
	{ "/geom/br", 2, { .f2 = geom_update }, { 64, 64 } },
	{ "/geom", 0, { .f0 = geom_readout } },
	{ "/geom/locktop", 1, { .f1 = geom_update_lock }, { 1 } },
	{ "/geom/lockbottom", 1, { .f1 = geom_update_lock }, { 1 } },
	{ "/geom/lockleft", 1, { .f1 = geom_update_lock }, { 1 } },
	{ "/geom/lockright", 1, { .f1 = geom_update_lock }, { 1 } },
)
