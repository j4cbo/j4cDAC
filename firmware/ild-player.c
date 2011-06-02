/* j4cDAC ILDA player
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

#define SMALL_FRAME_THRESHOLD	50
char ilda_sf_buffer[6 * SMALL_FRAME_THRESHOLD];

#include <ff.h>
#include <diskio.h>
#include <serial.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <dac.h>
#include <ilda-player.h>

static FIL ilda_file;

static int ilda_state;
static int ilda_points_left;
static int ilda_repeat_count;
static int ilda_offset;

static struct {
	unsigned int fptr;
	unsigned int curr_clust;
	unsigned int dsect;
} ilda_frame_start;

static int ilda_frame_pointcount;
static int ilda_points_per_frame;

static uint8_t const *ilda_palette_ptr;
static int ilda_palette_size;

int ilda_current_fps;

static const uint8_t ilda_palette_64[];
static const uint8_t ilda_palette_256[];

/* ilda_reset_file
 *
 * Return to the beginning of the current ILDA file.
 */
void ilda_reset_file(void) {
	ilda_state = -1;
	ilda_offset = 0;
	ilda_palette_ptr = ilda_palette_64;
	ilda_palette_size = 64;
	f_lseek(&ilda_file, 0);
}

void ilda_set_fps_limit(int max_fps) {
	ilda_current_fps = max_fps;
	ilda_points_per_frame = dac_current_pps / max_fps;
}

/* ilda_play_init
 *
 * Prepare the ILDA player to play a given file.
 */
int ilda_open(const char * fname) {
	FRESULT res = f_open(&ilda_file, fname, FA_READ);
	if (res) {
		outputf("ild_play: no file!");
		return -1;
	}

	ilda_reset_file();

	return 0;
}

/* ilda_read
 *
 * A thin wrapper around fatfs's f_read. Returns -1 if an error occurs,
 * otherwise returns the number of bytes read.
 */
static unsigned int ilda_read(uint8_t *buf, int n) {
	unsigned int bytes_read;

	FRESULT res = f_read(&ilda_file, buf, n, &bytes_read);
	if (res != FR_OK)
		return -1;

	return bytes_read;
}

/* ilda_read_frame_header
 *
 * Read the header for an ILDA frame (formats 0/1/4/5 all use the same
 * header). Returns the number of points in the frame.
 */
int ilda_read_frame_header(void) {
	uint8_t buf[8];

	/* Throw away "frame name" and "company name" */
	if (ilda_read(buf, 8) != 8) return -1;
	if (ilda_read(buf, 8) != 8) return -1;

	/* Read the rest of the header */
	if (ilda_read(buf, 8) != 8) return -1;

	/* The rest of the header contains total points, frame number,
	 * total frames, scanner head, and "future". We only care about
	 * the first. */
	int npoints = buf[0] << 8 | buf[1];

	/* Do we need to repeat this frame? */
	if (ilda_points_per_frame) {
		int points_needed = ilda_points_per_frame - ilda_offset;

		/* Round roughly halfway through the frame. */
		ilda_repeat_count = (points_needed + (npoints / 2)) / npoints;

		/* Don't drop frames. (Maybe we will in the future...) */
		if (ilda_repeat_count <= 0)
			ilda_repeat_count = 1;

		ilda_offset += (ilda_repeat_count * npoints) - ilda_points_per_frame;
	} else {
		ilda_repeat_count = 1;
	}

	ilda_points_left = npoints;

	return 0;
}

/* ilda_palette_point
 *
 * Parse a palettized color.
 */
void ilda_palette_point(dac_point_t *p, int color) {
	if (color & 0x4000) {
		/* "Blanking" flag */
		p->r = 0;
		p->g = 0;
		p->b = 0;
		p->i = 0;
	} else {
		/* Palette index */
		color &= 0xFF;
		if (color >= ilda_palette_size)
			color = ilda_palette_size - 1;

		p->r = ilda_palette_ptr[3 * color] << 8;
		p->g = ilda_palette_ptr[3 * color + 1] << 8;
		p->b = ilda_palette_ptr[3 * color + 2] << 8;

		int max = p->r;
		if (p->g > max) max = p->g;
		if (p->b > max) max = p->b;
		p->i = max;
	}
}

/* ilda_palette_point
 *
 * Parse a true-color point.
 */
void ilda_tc_point(dac_point_t *p, int r, int g, int b, int flags) {
	if (flags & 0x40) {
		/* "Blanking" flag */
		p->r = 0;
		p->g = 0;
		p->b = 0;
		p->i = 0;
	} else {
		p->r = r << 8;
		p->g = g << 8;
		p->b = b << 8;

		int max = r;
		if (g > max) max = g;
		if (b > max) max = b;
		p->i = max;
	}
}

/* ilda_read_points
 *
 * Produce up to max_points of output to be sent to the DAC.
 *
 * Returns 0 if the end of the file has been reached, -1 if an
 * error occurs, or the number of bytes actually written otherwise.
 */
int ilda_read_points(int max_points, packed_point_t *pp) {
	uint8_t buf[10];
	int i, x, y;

	if (ilda_state == -1) {
		/* We're currently betwee frames, so read the header. */
		int ret = ilda_read(buf, 8);
		if (ret == 0)
			return 0;
		else if (ret != 8)
			return -1;

		if (memcmp(buf, "ILDA\0\0\0", 7))
			return -1;

		ilda_state = buf[7];

		/* Read the rest of the header of this particular frame. */
		switch (ilda_state) {
		case 0:
		case 1:
		case 4:
		case 5:
			/* 2D, 3D, and formats 4 and 5 */
			ilda_read_frame_header();
			if (ilda_points_left < 0)
				return -1;

			/* Zero-length means end of file */
			if (ilda_points_left == 0)
				return 0;

			/* Save off the beginning of this frame, in case we
			 * need to repeat it. */
			ilda_frame_start.fptr = ilda_file.fptr;
			ilda_frame_start.curr_clust = ilda_file.curr_clust;
			ilda_frame_start.dsect = ilda_file.dsect;
			ilda_frame_pointcount = ilda_points_left;
			break;

		default:
			outputf("ilda: bad format %d", ilda_state);
			return -1;
		}
	}

	/* Now that we have some actual data, read from the file. */
	if (max_points > ilda_points_left)
		max_points = ilda_points_left;

	for (i = 0; i < max_points; i++) {
		int pt_num = ilda_frame_pointcount - ilda_points_left + i;
		char *sfb_ptr = ilda_sf_buffer + (6 * pt_num);
		dac_point_t p;

		switch (ilda_state) {
		case 0:
			/* 3D palettized */
			if (ilda_read(buf, 8) != 8) return -1;

			x = buf[0] << 8 | buf[1];
			y = buf[2] << 8 | buf[3];
			ilda_palette_point(&p, buf[6] << 8 | buf[7]);
			break;

		case 1:
			/* 2D palettized */
			if (ilda_read(buf, 6) != 6) return -1;

			x = buf[0] << 8 | buf[1];
			y = buf[2] << 8 | buf[3];
			ilda_palette_point(&p, buf[4] << 8 | buf[5]);
			break;

		case 4:
			/* 3D truecolor */
			if (ilda_read(buf, 10) != 10) return -1;
	
			x = buf[0] << 8 | buf[1];
			y = buf[2] << 8 | buf[3];
			ilda_tc_point(&p, buf[6], buf[7], buf[8], buf[9]);
			break;

		case 5:
			/* 2D truecolor */
			if (ilda_read(buf, 8) != 8) return -1;

			x = buf[0] << 8 | buf[1];
			y = buf[2] << 8 | buf[3];
			ilda_tc_point(&p, buf[4], buf[5], buf[6], buf[7]);
			break;

		case -2:
			/* Small frame replay */
			x = sfb_ptr[0] << 8 | (sfb_ptr[2] & 0xF0);
			y = sfb_ptr[1] << 8 | (sfb_ptr[2] << 4);
			p.r = sfb_ptr[3] << 8;
			p.g = sfb_ptr[4] << 8;
			p.b = sfb_ptr[5] << 8;
			int max = p.r;
			if (p.g > max) max = p.g;
			if (p.b > max) max = p.b;
			p.i = max;
			break;

		default:
			panic("ilda_state: bad value");
			return -1;
		}

		p.x = x;
		p.y = y;

		/* Save this point into the small frame buf, if needed */
		if (ilda_frame_pointcount <= SMALL_FRAME_THRESHOLD &&
		    ilda_state != -2) {
			sfb_ptr[0] = y >> 8;
			sfb_ptr[1] = x >> 8;
			sfb_ptr[2] = (x & 0xF0) | ((y & 0xF0) >> 4);
			sfb_ptr[3] = p.r >> 8;
			sfb_ptr[3] = p.g >> 8;
			sfb_ptr[3] = p.b >> 8;
		}

		dac_pack_point(pp, &p);

		pp++;
	}

	/* Now that we've read points, advance */
	ilda_points_left -= max_points;

	/* Do we need to move to the next frame, or repeat this one? */
	if (!ilda_points_left) {
		ilda_repeat_count--;
		if (!ilda_repeat_count) {
			ilda_state = -1;
		} else if (ilda_frame_pointcount <= SMALL_FRAME_THRESHOLD) {
			ilda_state = -2;
			ilda_points_left = ilda_frame_pointcount;
		} else {
			ilda_file.fptr = ilda_frame_start.fptr;
			ilda_file.curr_clust = ilda_frame_start.curr_clust;
			ilda_file.dsect = ilda_frame_start.dsect;
			ilda_points_left = ilda_frame_pointcount;

#if !_FS_TINY
			if (disk_read(ilda_file.fs->drv, ilda_file.buf, ilda_file.dsect, 1))
				return -1;
#endif
		}
	}

	return max_points;
}

static const uint8_t ilda_palette_64[] = {
	255,   0,   0, 255,  16,   0, 255,  32,   0, 255,  48,   0, 
	255,  64,   0, 255,  80,   0, 255,  96,   0, 255, 112,   0,
	255, 128,   0, 255, 144,   0, 255, 160,   0, 255, 176,   0,
	255, 192,   0, 255, 208,   0, 255, 224,   0, 255, 240,   0,
	255, 255,   0, 224, 255,   0, 192, 255,   0, 160, 255,   0,
	128, 255,   0,  96, 255,   0,  64, 255,   0,  32, 255,   0,
	  0, 255,   0,   0, 255,  32,   0, 255,  64,   0, 255,  96,
	  0, 255, 128,   0, 255, 160,   0, 255, 192,   0, 255, 224,
	  0, 130, 255,   0, 114, 255,   0, 104, 255,  10,  96, 255,
	  0,  82, 255,   0,  74, 255,   0,  64, 255,   0,  32, 255,
	  0,   0, 255,  32,   0, 255,  64,   0, 255,  96,   0, 255,
	128,   0, 255, 160,   0, 255, 192,   0, 255, 224,   0, 255,
	255,   0, 255, 255,  32, 255, 255,  64, 255, 255,  96, 255,
	255, 128, 255, 255, 160, 255, 255, 192, 255, 255, 224, 255,
	255, 255, 255, 255, 224, 224, 255, 192, 192, 255, 160, 160,
	255, 128, 128, 255,  96,  96, 255,  64,  64, 255,  32,  32
};

static const uint8_t ilda_palette_256[] = {
	  0,   0,   0, 255, 255, 255, 255,   0,   0, 255, 255,   0, 
	  0, 255,   0,   0, 255, 255,   0,   0, 255, 255,   0, 255, 
	255, 128, 128, 255, 140, 128, 255, 151, 128, 255, 163, 128, 
	255, 174, 128, 255, 186, 128, 255, 197, 128, 255, 209, 128, 
	255, 220, 128, 255, 232, 128, 255, 243, 128, 255, 255, 128, 
	243, 255, 128, 232, 255, 128, 220, 255, 128, 209, 255, 128, 
	197, 255, 128, 186, 255, 128, 174, 255, 128, 163, 255, 128, 
	151, 255, 128, 140, 255, 128, 128, 255, 128, 128, 255, 140, 
	128, 255, 151, 128, 255, 163, 128, 255, 174, 128, 255, 186, 
	128, 255, 197, 128, 255, 209, 128, 255, 220, 128, 255, 232, 
	128, 255, 243, 128, 255, 255, 128, 243, 255, 128, 232, 255, 
	128, 220, 255, 128, 209, 255, 128, 197, 255, 128, 186, 255, 
	128, 174, 255, 128, 163, 255, 128, 151, 255, 128, 140, 255, 
	128, 128, 255, 140, 128, 255, 151, 128, 255, 163, 128, 255, 
	174, 128, 255, 186, 128, 255, 197, 128, 255, 209, 128, 255, 
	220, 128, 255, 232, 128, 255, 243, 128, 255, 255, 128, 255, 
	255, 128, 243, 255, 128, 232, 255, 128, 220, 255, 128, 209, 
	255, 128, 197, 255, 128, 186, 255, 128, 174, 255, 128, 163, 
	255, 128, 151, 255, 128, 140, 255,   0,   0, 255,  23,   0, 
	255,  46,   0, 255,  70,   0, 255,  93,   0, 255, 116,   0, 
	255, 139,   0, 255, 162,   0, 255, 185,   0, 255, 209,   0, 
	255, 232,   0, 255, 255,   0, 232, 255,   0, 209, 255,   0, 
	185, 255,   0, 162, 255,   0, 139, 255,   0, 116, 255,   0, 
	 93, 255,   0,  70, 255,   0,  46, 255,   0,  23, 255,   0, 
	  0, 255,   0,   0, 255,  23,   0, 255,  46,   0, 255,  70, 
	  0, 255,  93,   0, 255, 116,   0, 255, 139,   0, 255, 162, 
	  0, 255, 185,   0, 255, 209,   0, 255, 232,   0, 255, 255, 
	  0, 232, 255,   0, 209, 255,   0, 185, 255,   0, 162, 255, 
	  0, 139, 255,   0, 116, 255,   0,  93, 255,   0,  70, 255, 
	  0,  46, 255,   0,  23, 255,   0,   0, 255,  23,   0, 255, 
	 46,   0, 255,  70,   0, 255,  93,   0, 255, 116,   0, 255, 
	139,   0, 255, 162,   0, 255, 185,   0, 255, 209,   0, 255, 
	232,   0, 255, 255,   0, 255, 255,   0, 232, 255,   0, 209, 
	255,   0, 185, 255,   0, 162, 255,   0, 139, 255,   0, 116, 
	255,   0,  93, 255,   0,  70, 255,   0,  46, 255,   0,  23, 
	128,  0,   0, 128,  12,   0, 128,  23,   0, 128,  35,   0, 
	128,  47,   0, 128,  58,   0, 128,  70,   0, 128,  81,   0, 
	128,  93,   0, 128, 105,   0, 128, 116,   0, 128, 128,   0, 
	116, 128,   0, 105, 128,   0,  93, 128,   0,  81, 128,   0, 
	 70, 128,   0,  58, 128,   0,  47, 128,   0,  35, 128,   0, 
	 23, 128,   0,  12, 128,   0,   0, 128,   0,   0, 128,  12, 
	  0, 128,  23,   0, 128,  35,   0, 128,  47,   0, 128,  58, 
	  0, 128,  70,   0, 128,  81,   0, 128,  93,   0, 128, 105, 
	  0, 128, 116,   0, 128, 128,   0, 116, 128,   0, 105, 128, 
	  0,  93, 128,   0,  81, 128,   0,  70, 128,   0,  58, 128, 
	  0,  47, 128,   0,  35, 128,   0,  23, 128,   0,  12, 128, 
	  0,   0, 128,  12,   0, 128,  23,   0, 128,  35,   0, 128, 
	 47,   0, 128,  58,   0, 128,  70,   0, 128,  81,   0, 128, 
	 93,   0, 128, 105,   0, 128, 116,   0, 128, 128,   0, 128, 
	128,   0, 116, 128,   0, 105, 128,   0,  93, 128,   0,  81, 
	128,   0,  70, 128,   0,  58, 128,   0,  47, 128,   0,  35, 
	128,   0,  23, 128,   0,  12, 255, 192, 192, 255,  64,  64, 
	192,   0,   0,  64,   0,   0, 255, 255, 192, 255, 255,  64, 
	192, 192,   0,  64,  64,   0, 192, 255, 192,  64, 255,  64, 
	  0, 192,   0,   0,  64,   0, 192, 255, 255,  64, 255, 255, 
	  0, 192, 192,   0,  64,  64, 192, 192, 255,  64,  64, 255, 
	  0,   0, 192,   0,   0,  64, 255, 192, 255, 255,  64, 255, 
	192,   0, 192,  64,   0,  64, 255,  96,  96, 255, 255, 255, 
	245, 245, 245, 235, 235, 235, 224, 224, 224, 213, 213, 213, 
	203, 203, 203, 192, 192, 192, 181, 181, 181, 171, 171, 171, 
	160, 160, 160, 149, 149, 149, 139, 139, 139, 128, 128, 128, 
	117, 117, 117, 107, 107, 107,  96,  96,  96,  85,  85,  85, 
	 75,  75,  75,  64,  64,  64,  53,  53,  53,  43,  43,  43, 
	 32,  32,  32,  21,  21,  21,  11,  11,  11,   0,   0,   0
};
