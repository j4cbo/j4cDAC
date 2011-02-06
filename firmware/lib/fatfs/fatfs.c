/* j4cDAC fatfs setup
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

#include <string.h>
#include <serial.h>
#include <tables.h>
#include <diskio.h>
#include <ff.h>

FATFS fs;

void sd_init(void) {
	char filename_buf[256];

	int res = disk_initialize(0);
	if (res) {
		outputf("SD initialization failed: %d", res);
		return;
	}

	/* This code sucks. It comes from the fatfs example code. */
	memset(&fs, 0, sizeof(fs));
	res = f_mount(0, &fs);
	if (res) {
		outputf("f_mount() failed: %d", res);
		return;
	}

	DIR dir;
	res = f_opendir(&dir, "");
	if (res)
		return;

	FILINFO finfo;
	int num_subdirs = 0, num_files = 0, total_size = 0;

	while (1) {
		finfo.lfname = filename_buf;
		finfo.lfsize = sizeof(filename_buf);
		res = f_readdir(&dir, &finfo);
		if ((res != FR_OK) || !finfo.fname[0])
			break;

		if (finfo.fattrib & AM_DIR) {
			num_subdirs++;
		} else {
			num_files++;
			total_size += finfo.fsize;
		}

		outputf("%c%c%c%c%c %u/%02u/%02u %02u:%02u %9lu  %s  %s",
			(finfo.fattrib & AM_DIR) ? 'D' : '-',
			(finfo.fattrib & AM_RDO) ? 'R' : '-',
			(finfo.fattrib & AM_HID) ? 'H' : '-',
			(finfo.fattrib & AM_SYS) ? 'S' : '-',
			(finfo.fattrib & AM_ARC) ? 'A' : '-',
			(finfo.fdate >> 9) + 1980, (finfo.fdate >> 5) & 15,
			finfo.fdate & 31, (finfo.ftime >> 11),
			(finfo.ftime >> 5) & 63, finfo.fsize,
			&(finfo.fname[0]), filename_buf);
	}
	outputf("%4u File(s),%10lu bytes total\n%4u Dir(s)",
		num_files, total_size, num_subdirs);
}

INITIALIZER(hardware, sd_init);
