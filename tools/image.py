#!/usr/bin/python
#
# j4cDAC flash tools
#
# Copyright 2010, 2011 Jacob Potter
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import intelhex
import binascii
import struct
import sys

APP_START = 0x4000

def main():
	if len(sys.argv) != 3:
		print "Usage: %s [hexfile] [outfile]" % (sys.argv[0], )
		sys.exit(1)

	hexfile = intelhex.IntelHex(sys.argv[1])

	if hexfile.minaddr() != APP_START:
		print "Error: image does not start at %x" % (APP_START, )
		sys.exit(1)

	# Find the actual data length
	imagelen = (hexfile.maxaddr() - APP_START + 64)
	imagelen -= (imagelen % 64)

	print "Image length: %d / %x" % (imagelen, imagelen)

	# Insert data length as vector 8
	data_len = struct.pack("<I", imagelen)
	hexfile.puts(APP_START + 0x20, data_len)

	# Extract the binary image data
	data = hexfile.tobinstr(start = APP_START)
	if len(data) % 64:
		data += (64 - (len(data) % 64)) * "\0"

	# Add a crc
	crc = struct.pack("<I", binascii.crc32(data) & 0xffffffff)
	data += crc + "\0" * 60

	# Tack on a header
	data = ("j4cDAC firmware image - DO NOT EDIT\n".ljust(59, "~")
	        + "\n" + data_len + data)

	file(sys.argv[2], "w").write(data)
	print "Wrote %d bytes." % (len(data), )

if __name__ == "__main__":
	main()
