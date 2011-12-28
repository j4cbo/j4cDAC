/* j4cDAC ILDA control via OSC
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

#include <osc.h>
#include <stdlib.h>
#include <string.h>
#include <tables.h>
#include <stdio.h>
#include <ff.h>
#include <dac.h>
#include <serial.h>
#include <playback.h>
#include <file_player.h>

/* walk_fs() does two different things:
 *
 * - If index <= 0:
 *     Read all ilda file names from the system and send them via OSC
 * - If index > 0:
 *     Open the index'th file. Note that index is 1-based.
 */
static void walk_fs(int index) {
	char filename_buf[64];
	char path[16];

	DIR dir;
	int res = f_opendir(&dir, "");
	if (res)
		return;

	FILINFO finfo;
	int i = 1;

	while (1) {
		finfo.lfname = filename_buf;
		finfo.lfsize = sizeof(filename_buf);
		res = f_readdir(&dir, &finfo);
		if ((res != FR_OK) || !finfo.fname[0])
			break;

		/* If it's anything other than a regular file, ignore it. */
		if (finfo.fattrib & AM_DIR) continue;
		if (finfo.fattrib & AM_HID) continue;
		if (finfo.fattrib & AM_SYS) continue;

		char * fn = *finfo.lfname ? finfo.lfname : finfo.fname;

		outputf("fn %s", fn);

		/* Does it end with .ild or .ilda? */
		char *fn_end = fn + strlen(fn);
		if (strcasecmp(fn_end - 4, ".ild") \
		    && strcasecmp(fn_end - 5, ".ilda"))
			continue;

		if (index <= 0) {
			/* Fuck you, TouchOSC. Fuck you in the ear. */
			if (strlen(fn) > 15)
				fn[15] = '\0';

			snprintf(path, sizeof(path), "/ilda/%d/name", i);
			osc_send_string(path, fn);
		} else if (index == i) {
			fplay_open(fn);
			playback_source_flags |= ILDA_PLAYER_PLAYING;
			break;
		}

		i++;
	}
}

static void refresh_display() {
	char buf[16];

	walk_fs(-1);

	outputf("pps: %d, fps: %d", dac_current_pps, ilda_current_fps);

	osc_send_int("/ilda/pps", dac_current_pps);
	osc_send_int("/ilda/fps", ilda_current_fps);

	snprintf(buf, sizeof(buf), "%dk", dac_current_pps / 1000);
	osc_send_string("/ilda/ppsreadout", buf);

	snprintf(buf, sizeof(buf), "%d", ilda_current_fps);
	osc_send_string("/ilda/fpsreadout", buf);
}

static void ilda_tab_enter_FPV_osc(const char *path) {
	refresh_display();
}

static void ilda_reload_FPV_osc(const char *path) {
	refresh_display();
}

void ilda_play_FPV_osc(const char *path) {
	/* Figure out which file index this was */
	int index = atoi(path + 6);

	outputf("play %d", index);

	/* Try switching to ILDA playback mode, if we weren't already */
	if (playback_set_src(SRC_ILDAPLAYER) < 0) {
		outputf("src switch err\n");
		return;
	}

	walk_fs(index);
}

static void ilda_pps_FPV_osc(const char *path, int32_t v) {
	char buf[6];
	snprintf(buf, sizeof(buf), "%ldk", v);
	osc_send_string("/ilda/ppsreadout", buf);

	if (playback_src != SRC_ILDAPLAYER)
		return;

	dac_set_rate(v);
	ilda_set_fps_limit(ilda_current_fps);
}

static void ilda_fps_FPV_osc(const char *path, int32_t v) {
	char buf[6];
	snprintf(buf, sizeof(buf), "%ld", v);
	osc_send_string("/ilda/fpsreadout", buf);

	ilda_set_fps_limit(v);
}

static void ilda_repeat_FPV_osc(const char *path, int32_t v) {
	if (playback_src != SRC_ILDAPLAYER)
		return;

	if (v)
		playback_source_flags |= ILDA_PLAYER_REPEAT;
	else
		playback_source_flags &= ~ILDA_PLAYER_REPEAT;
}

static void ilda_stop_FPV_osc(const char *path) {
	playback_source_flags &= ~ILDA_PLAYER_PLAYING;
	dac_stop(0);
}

TABLE_ITEMS(param_handler, ilda_osc_handlers,
	{ "/ilda/1/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/2/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/3/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/4/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/5/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/6/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/7/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/8/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/9/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/10/play", PARAM_TYPE_0, { .f0 = ilda_play_FPV_osc } },
	{ "/ilda/reloadbutton", PARAM_TYPE_0, { .f0 = ilda_reload_FPV_osc } },
	{ "/ilda/pps", PARAM_TYPE_I1, { .f1 = ilda_pps_FPV_osc }, PARAM_MODE_INT, 1000, 100000 },
	{ "/ilda/fps", PARAM_TYPE_I1, { .f1 = ilda_fps_FPV_osc }, PARAM_MODE_INT, 0, 100 },
	{ "/ilda/repeat", PARAM_TYPE_I1, { .f1 = ilda_repeat_FPV_osc }, PARAM_MODE_INT },
	{ "/ilda", PARAM_TYPE_0, { .f0 = ilda_tab_enter_FPV_osc } },
	{ "/stop", PARAM_TYPE_0, { .f0 = ilda_stop_FPV_osc } },
)
