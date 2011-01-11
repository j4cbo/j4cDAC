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

# This is an unfinished tool to frob the Flash check word in an image. It
# is not currently used - lpc17isp will do this manipulation for us - but
# may be needed in the future.

import intelhex
import serial 
import struct
import sys


def lpc1758_fixup(hexfile):
	# Read vectors 0 through 6
	checksum = 0
	for i in xrange(0, 7):
		vec_data = hexfile.gets(i * 4, 4)
		(vec_value, ) = struct.unpack("<I", vec_data)
		checksum -= vec_value

	# Set vector 7 to have an appropriate checksum
	new_data = struct.pack("<I", checksum % 2**32)
	hexfile.puts(0x1C, new_data)


def main():
	if len(sys.argv) != 3:
		print "Usage: %s [hexfile] [outfile]" % (sys.argv[0], )
		sys.exit(1)

	hexfile = intelhex.IntelHex(sys.argv[1])

	lpc1758_fixup(hexfile)

	hexfile.tofile(sys.argv[2], format = "hex")


if __name__ == "__main__":
	main()
