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
#include <attrib.h>
#include <serial.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <dac.h>
#include <file_player.h>
#include <ff.h>
#include <LPC17xx.h>

#define SMALL_FRAME_THRESHOLD	200

struct sfb {
	uint16_t x;
	uint16_t y;
	uint8_t i;
	uint8_t r;
	uint8_t g;
	uint8_t b;
} fplay_small_frame_buffer[SMALL_FRAME_THRESHOLD] AHB0;

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

int fplay_error_detail;

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

#define BAIL(s)	return -((int)s)
#define BAILV(s, v) do { fplay_error_detail=(v); return -((int)s); } while(0)

/* fplay_read
 *
 * A thin wrapper around fatfs's f_read. Bails out if a fatfs error
 * occurs; otherwise, returns the number of bytes read.
 */
static unsigned int fplay_read(void *buf, int n) {
	unsigned int bytes_read;

	FRESULT res = f_read(&fplay_file, buf, n, &bytes_read);
	if (res != FR_OK) {
		fplay_error_detail = res;
		BAIL("fplay_read: fatfs err %d");
	}

	return bytes_read;
}

/* fplay_read_check
 *
 * A helper macro around fplay_read: checks that the number of bytes read
 * was as expected.
 */
#define fplay_read_check(addr, len) do {	\
	int res = fplay_read((addr), (len));	\
	if (res < 0) return res;		\
	if (res != (len)) BAIL("short read");	\
} while(0)

/* wav_read_file_header
 *
 * Read the header for a WAV file, and save playback information.
 */
static int wav_read_file_header(int file_size) {

	/* fmt chunk */
	struct wav_header_part2 {
		uint32_t fmt_size;
		uint16_t audio_format;
		uint16_t num_channels;
		uint32_t sample_rate;
		uint32_t byte_rate;
		uint16_t block_align;
		uint16_t bits_per_sample;
	} __attribute__((packed)) pt2;
	fplay_read_check(&pt2, sizeof(pt2));

	if (pt2.fmt_size < 16) BAILV("audio fmt chunk too short: %d bytes", pt2.fmt_size);

	int is_extended;

	if (pt2.audio_format == 1)
		is_extended = 0;
	else if (pt2.audio_format == 0xFFFE)
		is_extended = 1;
	else
		BAILV("bad audio fmt: 0x%04x", pt2.audio_format);

	if (pt2.num_channels < 5 || pt2.num_channels > 8)
		BAILV("bad channel count: %d", pt2.num_channels);
	wav_channels = pt2.num_channels;
	int point_rate = pt2.sample_rate;
	if (pt2.byte_rate != point_rate * wav_channels * 2)
		BAIL("byte rate mismatch");
	if (pt2.block_align < wav_channels * 2)
		BAILV("block align too small: %d", pt2.block_align);
	if (pt2.block_align > 16)
		BAILV("block align too large: %d", pt2.block_align);
	wav_block_align = pt2.block_align;
	if (pt2.bits_per_sample != 16)
		BAIL("16-bit samples required");

	char buf[8];
	int fmt_size_left = pt2.fmt_size - 16;

	if (fmt_size_left >= 22 && is_extended) {
		fplay_read_check(buf, 8);
		uint16_t actual_type;
		fplay_read_check(&actual_type, 2);
		if (actual_type != 1)
			BAILV("bad extended audio fmt: 0x%04x", actual_type);
		fmt_size_left -= 10;
	} else if (is_extended)
		BAILV("extended hdr too short: need 22 bytes, got %d", fmt_size_left);

	/* Consume the rest of the format block, if any */
	while (fmt_size_left > 0) {
		fplay_read_check(buf, fmt_size_left > 8 ? 8 : fmt_size_left);
		fmt_size_left -= 8;
	}

	/* Now read data header */
	fplay_read_check(buf, 8);

	/* Ignore fact chunk if present */
	if (!memcmp(buf, "fact", 4)) {
		fplay_read_check(buf, 4);
		fplay_read_check(buf, 8);
	}
	
	if (memcmp(buf, "data", 4)) BAIL("no data header");

	int data_size = *(uint32_t *)(buf + 4);
	if (data_size > file_size - 36) data_size = file_size - 36;

	fplay_points_left = data_size / wav_block_align;
	ilda_frame_pointcount = fplay_points_left;
	fplay_repeat_count = 1;
	fplay_state = STATE_WAV;

	dac_set_rate(point_rate);

	/* Phew! */
	return 1;
}

/* ilda_read_frame_header
 *
 * Read the header for an ILDA frame (formats 0/1/4/5 all use the same
 * header). Returns the number of points in the frame.
 */
static int ilda_read_frame_header(void) {
	uint8_t buf[8];

	/* Throw away "frame name" and "company name" */
	fplay_read_check(buf, 8);
	fplay_read_check(buf, 8);

	/* Read the rest of the header */
	fplay_read_check(buf, 8);

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
	p->u2 = words[7];
	p->u1 = words[6];
	p->i = words[5];
	p->x = words[0];
	p->y = words[1];
	p->r = words[2];
	p->g = words[3];
	p->b = words[4];
}


/* fplay_read_header
 *
 * Read the header out of a file, and determine if it is WAV or ILDA.
 */
int fplay_read_header(void) {
	char buf[8];

	int ret = fplay_read(buf, 8);
	if (ret == 0) return 0;
	else if (ret != 8) BAIL("short read");

	if (!memcmp(buf, "RIFF", 4)) {
		uint32_t wav_file_size = *(uint32_t *)(buf + 4);
		fplay_read_check(buf, 8);
		if (memcmp(buf, "WAVEfmt ", 8)) BAIL("RIFF file not WAV");
		return wav_read_file_header(wav_file_size);
	}

	/* If it's not WAV, it had better be ILDA */
	if (memcmp(buf, "ILDA\0\0\0", 7))
		BAIL("file is not WAV or ILDA");

	fplay_state = buf[7];

	/* Read the rest of the header of this particular frame. */
	switch (fplay_state) {
	case STATE_ILDA_0:
	case STATE_ILDA_1:
	case STATE_ILDA_4:
	case STATE_ILDA_5:
		/* 2D, 3D, and formats 4 and 5 */
		ret = ilda_read_frame_header();
		if (ret < 0) return ret;
		if (fplay_points_left < 0) BAIL("negative points in frame");

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
		BAILV("ilda: bad format %d", fplay_state);
	}

	return 1;
}

#define ILDA_MAX_POINTS_PER_LOOP	25

/* ilda_read_points
 *
 * Produce up to max_points of output to be sent to the DAC.
 *
 * Returns 0 if the end of the file has been reached, -1 if an
 * error occurs, or the number of bytes actually written otherwise.
 */

static inline void save_small_frame(struct sfb *sfb, struct dac_point *p) {
	if (ilda_frame_pointcount <= SMALL_FRAME_THRESHOLD) {
		sfb->x = p->x;
		sfb->y = p->y;
		sfb->i = p->i >> 8;
		sfb->r = p->r >> 8;
		sfb->g = p->g >> 8;
		sfb->b = p->b >> 8;
	}
}

int NOINLINE ilda_do_read_points(int points, packed_point_t *pp);

int ilda_read_points(int points, packed_point_t *pp) {
	if (fplay_state == STATE_BETWEEN_FRAMES) {
		int res = fplay_read_header();
		if (res <= 0) return res;
	}

	return ilda_do_read_points(points, pp);
}

int NOINLINE ilda_do_read_points(int points, packed_point_t *pp) {
	int i;

	union {
		uint16_t words[8 * ILDA_MAX_POINTS_PER_LOOP];
		uint8_t bytes[16 * ILDA_MAX_POINTS_PER_LOOP];
	} ilda_buffer;

	/* Now that we have some actual data, read from the file. */
	if (points > fplay_points_left)
		points = fplay_points_left;

	if (points > ILDA_MAX_POINTS_PER_LOOP)
		points = ILDA_MAX_POINTS_PER_LOOP;

	dac_point_t p = { 0 };
	int pt_num = ilda_frame_pointcount - fplay_points_left;
	struct sfb* sfb_ptr = fplay_small_frame_buffer + pt_num;

	switch (fplay_state) {
	case STATE_ILDA_0:
		/* 3D w/ palette */
		fplay_read_check(ilda_buffer.bytes, 8 * points);
		for (i = 0; i < points; i++) {
			p.x = rev16(ilda_buffer.words[4*i]);
			p.y = rev16(ilda_buffer.words[4*i + 1]);
			ilda_palette_point(&p, rev16(ilda_buffer.words[4*i + 3]));
			save_small_frame(sfb_ptr, &p);
			dac_pack_point(pp, &p);
			sfb_ptr++;
			pp++;
		}

		break;

	case STATE_ILDA_1:
		/* 2D w/ palette */
		fplay_read_check(ilda_buffer.bytes, 6 * points);
		for (i = 0; i < points; i++) {
			p.x = rev16(ilda_buffer.words[3*i]);
			p.y = rev16(ilda_buffer.words[3*i + 1]);
			ilda_palette_point(&p, rev16(ilda_buffer.words[3*i + 2]));
			save_small_frame(sfb_ptr, &p);
			dac_pack_point(pp, &p);
			sfb_ptr++;
			pp++;
		}
		break;
	
	case STATE_ILDA_4:
		/* 3D truecolor */
		fplay_read_check(ilda_buffer.bytes, 10 * points);
		for (i = 0; i < points; i++) {
			p.x = rev16(ilda_buffer.words[5*i]);
			p.y = rev16(ilda_buffer.words[5*i + 1]);
			uint8_t *b = ilda_buffer.bytes + 10*i;
			ilda_tc_point(&p, b[9], b[8], b[7], b[6]);
			save_small_frame(sfb_ptr, &p);
			dac_pack_point(pp, &p);
			sfb_ptr++;
			pp++;
		}
		break;

	case STATE_ILDA_5:
		/* 2D truecolor */
		fplay_read_check(ilda_buffer.bytes, 8 * points);
		for (i = 0; i < points; i++) {
			p.x = rev16(ilda_buffer.words[4*i]);
			p.y = rev16(ilda_buffer.words[4*i + 1]);
			uint8_t *b = ilda_buffer.bytes + 8*i;
			ilda_tc_point(&p, b[7], b[6], b[5], b[4]);
			save_small_frame(sfb_ptr, &p);
			dac_pack_point(pp, &p);
			sfb_ptr++;
			pp++;
		}
		break;

	case STATE_WAV:
		/* WAV */
		fplay_read_check(ilda_buffer.bytes, wav_block_align * points);
		for (i = 0; i < points; i++) {
			wav_parse_16bit_point(&p, ilda_buffer.words + i * 8);
			dac_pack_point(pp, &p);
			pp++;
		}
		break;

	case STATE_SMALL_FRAME:
		/* Small frame replay */
		for (i = 0; i < points; i++) {
			p.x = sfb_ptr->x;
			p.y = sfb_ptr->y;
			p.i = sfb_ptr->i << 8;
			p.r = sfb_ptr->r << 8;
			p.g = sfb_ptr->g << 8;
			p.b = sfb_ptr->b << 8;
			sfb_ptr ++;
			dac_pack_point(pp, &p);
			pp++;
		}
		break;

	default:
		panic("fplay_state: bad value");
	}

	/* Now that we've read points, advance */
	fplay_points_left -= points;

	/* Do we need to move to the next frame, or repeat this one? */
	if (!fplay_points_left) {
		fplay_repeat_count--;
		if (!fplay_repeat_count) {
			fplay_state = STATE_BETWEEN_FRAMES;
		} else if (ilda_frame_pointcount <= SMALL_FRAME_THRESHOLD) {
			fplay_state = STATE_SMALL_FRAME;
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

	return points;
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
