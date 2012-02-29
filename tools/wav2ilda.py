#!/usr/bin/python
#
# wav2ilda
#
# Copyright 2012 Jacob Potter
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

import getopt
import struct
import sys

class WavFile(object):
	def __init__(self, f):
		self.f = f

		if f.read(4) != "RIFF":
			raise Exception("no RIFF header - is this a WAV file?")

		wav_file_size = struct.unpack("<L", f.read(4))

		if f.read(8) != "WAVEfmt ":
			raise Exception("no WAVE / fmt header")

		fmt_size, audio_format, self.num_channels, self.sample_rate, \
		byte_rate, block_align, bits_per_sample = struct.unpack("<LHHLLHH",
			f.read(20))

		if audio_format == 1:
			is_extended = 0
		elif audio_format == 0xFFFE:
			is_extended = 1
		else:
			raise Exception("unknown format %d" % (audio_format, ))

		if self.num_channels < 5 or self.num_channels > 8:
			raise Exception("bad channel count %d" % (self.num_channels, ))

		if bits_per_sample not in (16, 24):
			raise Exception("bad bits per sample: %d" % (bits_per_sample, ))

		self.bytes_per_sample = bits_per_sample / 8

		if byte_rate != self.sample_rate * self.num_channels * self.bytes_per_sample:
			raise Exception("bad byte rate: expected %d got %d" % (
				self.sample_rate * self.num_channels * 2, byte_rate))

		if block_align < self.num_channels * self.bytes_per_sample or block_align > 32:
			raise Exception("bad block alignment: %d" % (block_align, ))

		fmt_size_left = fmt_size - 16

		if fmt_size_left >= 22 and is_extended:
			f.read(8)
			actual_type, = struct.unpack("<H", f.read(2))
			if actual_type != 1:
				raise Exception("unknown extended format %d" % (actual_type, ))
			fmt_size_left -= 10
		elif is_extended:
			raise Exception("extended format, but insufficient bytes in fmt chunk")

		f.read(fmt_size_left)

		print "Loaded WAV file: %d bits, %d kHz, %d channels" % (
			bits_per_sample, self.sample_rate, self.num_channels)

		next = f.read(4)
		if next == "fact":
			f.read(8)
			next = f.read(4)

		if next != "data":
			raise Exception("expected data chunk, got %r" % (next, ))

		data_size = struct.unpack("<I", f.read(4))
		self.block_align = block_align

		if bits_per_sample == 16:
			self.unpack_format = "<" + ("h" * self.num_channels)
		else:
			self.unpack_format = "<" + ("xh" * self.num_channels)


	def read_points(self):
		fmt = self.unpack_format
		while True:
			block = self.f.read(self.block_align)[:(self.bytes_per_sample * self.num_channels)]
			if not block:
				return
			yield struct.unpack(fmt, block)


def read_from_stream(stream, n):
	for i in xrange(n):
		yield stream.next()

def clamp(x):
	if x < 0: x = 0
	if x > 255: x = 255
	return x

def usage():
	print "Usage: %s [-x] [-y] [-s] [-i]" % (sys.argv[0], )
	print ""
	print "\t-x: flip output around the X axis"
	print "\t-y: flip output around the Y axis"
	print "\t-s: swap the X and Y axes"
	print "\t-i: invert the color channel modulation"
	sys.exit(0)

def main(argv):
	try:
		opts, fs = getopt.gnu_getopt(argv[1:], "xysih")
	except getopt.GetoptError, e:
		usage()
	switches = [ s for s, v in opts if not v ]
	if "-h" in switches:
		usage()
	if len(fs) != 1:
		usage()

	fname = fs[0]
	f = file(fname, "r")
	w = WavFile(f)
	points_per_frame = w.sample_rate / 30
	points = w.read_points()
	count = 0

	out = file(fname + ".ild", "w")

	print "Converting %s..." % (fname, )

	while True:
		count += 1
		group = list(read_from_stream(points, points_per_frame))
		if not group:
			break

		hdr = "ILDA" + struct.pack(">L", 5)
		hdr += "wav2ildawav2ilda" + struct.pack(">HHHBB", len(group), count, 1000, 0, 0)
		out.write(hdr)

		for pt in group:
			x = pt[0]
			y = pt[1]

			if "-x" in switches:
				x, y = y, x
			if "-x" in switches:
				x = -1 - x
			if "-y" in switches:
				y = -1 - y

			r = (pt[2] + 32768) >> 8
			g = (pt[3] + 32768) >> 8
			b = (pt[4] + 32768) >> 8

			if "-i" in switches:
				r = 0x80 - r
				g = 0x80 - g
				b = 0x80 - b 
			else:
				r = r - 0x80
				g = g - 0x80
				b = b - 0x80

			r = clamp(r * 2)
			g = clamp(g * 2)
			b = clamp(b * 2)

			out.write(struct.pack(">hhBBBB", x, y, 0, b, g, r))

	print "Produed %d frames." % (count, )
	return 0

if __name__ == "__main__":
	sys.exit(main(sys.argv))
