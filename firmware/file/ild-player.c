/* j4cDAC SD card file player
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

#include <diskio.h>
#include <serial.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <dac.h>
#include <file_player.h>
#include <ff.h>

#define SMALL_FRAME_THRESHOLD	100

char fplay_small_frame_buffer[6 * SMALL_FRAME_THRESHOLD];

static FIL fplay_file;
static enum {
	STATE_WAV = -3,
	STATE_SMALL_FRAME = -2,
	STATE_BETWEEN_FRAMES = -1,
	STATE_ILDA_0 = 0,
	STATE_ILDA_1 = 1,
	STATE_ILDA_2 = 2,
	STATE_ILDA_3 = 3,
	STATE_ILDA_4 = 4,
	STATE_ILDA_5 = 5,
} fplay_state;

static int fplay_points_left;
static int fplay_repeat_count;
static int fplay_offset;

static struct {
	unsigned int fptr;
	unsigned int curr_clust;
	unsigned int dsect;
} fplay_frame_start;

static int ilda_frame_pointcount;
static int ilda_points_per_frame;
static uint8_t wav_channels;
static uint8_t wav_block_align;

static uint8_t const *ilda_palette_ptr;
static int ilda_palette_size;

int ilda_current_fps;

static const uint8_t ilda_palette_64[];
static const uint8_t ilda_palette_256[];

/* calculate_intensity
 *
 * Calculate the intensity of a point given r, g, and b.
 */
static void calculate_intensity(dac_point_t *p) {
	int max = p->r;
	if (p->g > max) max = p->g;
	if (p->b > max) max = p->b;
	p->i = max;
}

/* ilda_reset_file
 *
 * Return to the beginning of the current ILDA file.
 */
void ilda_reset_file(void) {
	fplay_state = STATE_BETWEEN_FRAMES;
	fplay_offset = 0;
	ilda_palette_ptr = ilda_palette_64;
	ilda_palette_size = 64;
	f_lseek(&fplay_file, 0);
}

void ilda_set_fps_limit(int max_fps) {
	ilda_current_fps = max_fps;
	ilda_points_per_frame = dac_current_pps / max_fps;
}

/* fplay_open
 *
 * Prepare the ILDA player to play a given file.
 */
int fplay_open(const char * fname) {
	FRESULT res = f_open(&fplay_file, fname, FA_READ);
	if (res) {
		outputf("ild_play: no file: %d", res);
		return -1;
	}

	ilda_reset_file();

	return 0;
}

/* fplay_read
 *
 * A thin wrapper around fatfs's f_read. Returns -1 if an error occurs,
 * otherwise returns the number of bytes read.
 */
static unsigned int fplay_read(void *buf, int n) {
	unsigned int bytes_read;

	FRESULT res = f_read(&fplay_file, buf, n, &bytes_read);
	if (res != FR_OK)
		return -1;

	return bytes_read;
}

struct wav_header_part2 {
	uint32_t fmt_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
} __attribute__((packed));

/* wav_read_file_header
 *
 * Read the header for a WAV file, and save playback information.
 */
static int wav_read_file_header(int file_size) {

	/* fmt chunk */
	struct wav_header_part2 pt2;
	if (fplay_read(&pt2, sizeof(pt2)) != sizeof(pt2)) return -1;

	if (pt2.fmt_size < 16) return -1;
	if (pt2.audio_format != 1) return -1;
	if (pt2.num_channels < 5 || pt2.num_channels > 8) return -1;
	wav_channels = pt2.num_channels;
	int point_rate = pt2.sample_rate;
	if (pt2.byte_rate != point_rate * wav_channels * 2) return -1;
	if (pt2.block_align < wav_channels * 2 || pt2.block_align > 16)
		return -1;
	wav_block_align = pt2.block_align;
	if (pt2.bits_per_sample != 16) return -1;

	char buf[8];

	/* Consume the rest of the format block, if any */
	int fmt_size_left = pt2.fmt_size - 16;
	while (fmt_size_left > 0) {
		fplay_read(buf, fmt_size_left > 8 ? 8 : fmt_size_left);
		fmt_size_left -= 8;
	}

	/* Now read data header */
	if (fplay_read(buf, 8) != 8) return -1;
	if (memcmp(buf, "data", 4)) return -1;

	int data_size = *(uint32_t *)(buf + 4);
	if (data_size > file_size - 36) data_size = file_size - 36;

	fplay_points_left = data_size / wav_block_align;
	fplay_repeat_count = 1;

	/* Phew! */
	return 0;
}

/* ilda_read_frame_header
 *
 * Read the header for an ILDA frame (formats 0/1/4/5 all use the same
 * header). Returns the number of points in the frame.
 */
static int ilda_read_frame_header(void) {
	uint8_t buf[8];

	/* Throw away "frame name" and "company name" */
	if (fplay_read(buf, 8) != 8) return -1;
	if (fplay_read(buf, 8) != 8) return -1;

	/* Read the rest of the header */
	if (fplay_read(buf, 8) != 8) return -1;

	/* The rest of the header contains total points, frame number,
	 * total frames, scanner head, and "future". We only care about
	 * the first. */
	int npoints = buf[0] << 8 | buf[1];

	/* Do we need to repeat this frame? */
	if (ilda_points_per_frame) {
		int points_needed = ilda_points_per_frame - fplay_offset;

		/* Round roughly halfway through the frame. */
		fplay_repeat_count = (points_needed + (npoints / 2)) / npoints;

		/* Don't drop frames. (Maybe we will in the future...) */
		if (fplay_repeat_count <= 0)
			fplay_repeat_count = 1;

		fplay_offset += (fplay_repeat_count * npoints) - ilda_points_per_frame;
	} else {
		fplay_repeat_count = 1;
	}

	outputf("p %d x%d", npoints, fplay_repeat_count);
	fplay_points_left = npoints;

	return 0;
}

/* ilda_palette_point
 *
 * Parse a palettized color.
 */
static void ilda_palette_point(dac_point_t *p, uint16_t color) {
	if (color & 0x4000) {
		/* "Blanking" flag */
		p->r = 0;
		p->g = 0;
		p->b = 0;
		p->i = 0;
	} else {
		/* Palette index */
		if (color >= ilda_palette_size)
			color = ilda_palette_size - 1;

		p->r = ilda_palette_ptr[3 * color] << 8;
		p->g = ilda_palette_ptr[3 * color + 1] << 8;
		p->b = ilda_palette_ptr[3 * color + 2] << 8;
		calculate_intensity(p);
	}
}

/* ilda_tc_point
 *
 * Parse a true-color point.
 */
static void ilda_tc_point(dac_point_t *p, int r, int g, int b, int flags) {
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
		calculate_intensity(p);
	}
}

/* wav_parse_16bit_point
 *
 * Parse a point.
 */
static void wav_parse_16bit_point(dac_point_t *p, uint16_t *words) {
	switch (wav_channels) {
	case 8:
		p->u2 = words[7];
	case 7:
		p->u1 = words[6];
	case 6:
		p->i = words[5];
	default:
		p->x = words[0];
		p->y = words[1];
		p->r = words[2];
		p->g = words[3];
		p->b = words[4];
	}
}


/* fplay_read_header
 *
 * Read the header out of a file, and determine if it is WAV or ILDA.
 */
int fplay_read_header(void) {
	char buf[8];

	int ret = fplay_read(buf, 8);
	if (ret == 0)
		return 0;
	else if (ret != 8)
		return -1;

	if (!memcmp(buf, "RIFF", 4)) {
		uint32_t wav_file_size = *(uint32_t *)(buf + 4);
		if (fplay_read(buf, 8) != 8) return -1;
		if (memcmp(buf, "WAVEfmt ", 8)) return -1;
		return wav_read_file_header(wav_file_size);
	}

	/* If it's not WAV, it had better be ILDA */
	if (memcmp(buf, "ILDA\0\0\0", 7))
		return -1;

	fplay_state = buf[7];

	/* Read the rest of the header of this particular frame. */
	switch (fplay_state) {
	case STATE_ILDA_0:
	case STATE_ILDA_1:
	case STATE_ILDA_4:
	case STATE_ILDA_5:
		/* 2D, 3D, and formats 4 and 5 */
		if (ilda_read_frame_header() < 0)
			return -1;
		if (fplay_points_left < 0)
			return -1;

		/* Zero-length means end of file */
		if (fplay_points_left == 0)
			return 0;

		/* Save off the beginning of this frame, in case we
		 * need to repeat it. */
		fplay_frame_start.fptr = fplay_file.fptr;
		fplay_frame_start.curr_clust = fplay_file.curr_clust;
		fplay_frame_start.dsect = fplay_file.dsect;
		ilda_frame_pointcount = fplay_points_left;
		break;

	default:
		outputf("ilda: bad format %d", fplay_state);
		return -1;
	}

	return 0;
}

/* ilda_read_points
 *
 * Produce up to max_points of output to be sent to the DAC.
 *
 * Returns 0 if the end of the file has been reached, -1 if an
 * error occurs, or the number of bytes actually written otherwise.
 */
int ilda_read_points(int max_points, packed_point_t *pp) {
	uint8_t buf[16];
	int i;

	if (fplay_state == STATE_BETWEEN_FRAMES) {
		if (fplay_read_header() < 0)
			return -1;
	}

	/* Now that we have some actual data, read from the file. */
	if (max_points > fplay_points_left)
		max_points = fplay_points_left;

	for (i = 0; i < max_points; i++) {
		int pt_num = ilda_frame_pointcount - fplay_points_left + i;
		char *sfb_ptr = fplay_small_frame_buffer + (6 * pt_num);
		dac_point_t p = { 0 };

		switch (fplay_state) {
		case STATE_ILDA_0:
			/* 3D palettized */
			if (fplay_read(buf, 8) != 8) return -1;

			p.x = buf[0] << 8 | buf[1];
			p.y = buf[2] << 8 | buf[3];
			ilda_palette_point(&p, buf[6] << 8 | buf[7]);
			break;

		case STATE_ILDA_1:
			/* 2D palettized */
			if (fplay_read(buf, 6) != 6) return -1;

			p.x = buf[0] << 8 | buf[1];
			p.y = buf[2] << 8 | buf[3];
			ilda_palette_point(&p, buf[4] << 8 | buf[5]);
			break;

		case STATE_ILDA_4:
			/* 3D truecolor */
			if (fplay_read(buf, 10) != 10) return -1;
	
			p.x = buf[0] << 8 | buf[1];
			p.y = buf[2] << 8 | buf[3];
			ilda_tc_point(&p, buf[7], buf[8], buf[9], buf[6]);
			break;

		case STATE_ILDA_5:
			/* 2D truecolor */
			if (fplay_read(buf, 8) != 8) return -1;

			p.x = buf[0] << 8 | buf[1];
			p.y = buf[2] << 8 | buf[3];
			ilda_tc_point(&p, buf[5], buf[6], buf[7], buf[4]);
			break;

		case STATE_WAV:
			/* WAV */
			if (fplay_read(buf, wav_block_align)
				!= wav_block_align) return -1;
			wav_parse_16bit_point(&p, (uint16_t *)buf);
			break;

		case STATE_SMALL_FRAME:
			/* Small frame replay */
			p.x = sfb_ptr[0] << 8 | (sfb_ptr[2] & 0xF0);
			p.y = sfb_ptr[1] << 8 | (sfb_ptr[2] << 4);
			p.r = sfb_ptr[3] << 8;
			p.g = sfb_ptr[4] << 8;
			p.b = sfb_ptr[5] << 8;
			int max = p.r;
			if (p.g > max) max = p.g;
			if (p.b > max) max = p.b;
			p.i = max;
			break;

		default:
			panic("fplay_state: bad value");
			return -1;
		}

		/* Save this point into the small frame buf, if needed */
		if (ilda_frame_pointcount <= SMALL_FRAME_THRESHOLD &&
		    fplay_state != STATE_SMALL_FRAME) {
			sfb_ptr[0] = p.y >> 8;
			sfb_ptr[1] = p.x >> 8;
			sfb_ptr[2] = (p.x & 0xF0) | ((p.y & 0xF0) >> 4);
			sfb_ptr[3] = p.r >> 8;
			sfb_ptr[3] = p.g >> 8;
			sfb_ptr[3] = p.b >> 8;
		}

		dac_pack_point(pp, &p);

		pp++;
	}

	/* Now that we've read points, advance */
	fplay_points_left -= max_points;

	/* Do we need to move to the next frame, or repeat this one? */
	if (!fplay_points_left) {
		fplay_repeat_count--;
		if (!fplay_repeat_count) {
			fplay_state = -1;
		} else if (ilda_frame_pointcount <= SMALL_FRAME_THRESHOLD) {
			fplay_state = -2;
			fplay_points_left = ilda_frame_pointcount;
		} else {
			fplay_file.fptr = fplay_frame_start.fptr;
			fplay_file.curr_clust = fplay_frame_start.curr_clust;
			fplay_file.dsect = fplay_frame_start.dsect;
			fplay_points_left = ilda_frame_pointcount;

#if !_FS_TINY
			if (disk_read(fplay_file.fs->drv, fplay_file.buf, fplay_file.dsect, 1))
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
