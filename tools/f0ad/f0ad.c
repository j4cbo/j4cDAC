/* j4cDAC reenter-bootloader tool
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

#include <libusb-1.0/libusb.h>
#include <stdio.h>

int main() {
	libusb_init(NULL);
	libusb_set_debug(NULL, 3);

	libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, 0xffff, 0x0005);

	if (!h) {
		printf("No device found.\n");
		return -1;
	}

	printf("F0AD!\n");
	libusb_control_transfer(h, LIBUSB_REQUEST_TYPE_STANDARD,
		LIBUSB_REQUEST_SET_FEATURE, 0xF0AD, 0xF0AD, NULL, 0, 0);

	return 0;
}
