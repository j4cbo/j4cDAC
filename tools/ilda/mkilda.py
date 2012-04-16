import sys
import struct
from math import pi, sin, cos

color_max = 255

ilda_palette_64 = (
	( 255,   0,   0 ), ( 255,  16,   0 ), ( 255,  32,   0 ), ( 255,  48,   0 ),
	( 255,  64,   0 ), ( 255,  80,   0 ), ( 255,  96,   0 ), ( 255, 112,   0 ),
	( 255, 128,   0 ), ( 255, 144,   0 ), ( 255, 160,   0 ), ( 255, 176,   0 ),
	( 255, 192,   0 ), ( 255, 208,   0 ), ( 255, 224,   0 ), ( 255, 240,   0 ),
	( 255, 255,   0 ), ( 224, 255,   0 ), ( 192, 255,   0 ), ( 160, 255,   0 ),
	( 128, 255,   0 ), (  96, 255,   0 ), (  64, 255,   0 ), (  32, 255,   0 ),
	(   0, 255,   0 ), (   0, 255,  32 ), (   0, 255,  64 ), (   0, 255,  96 ),
	(   0, 255, 128 ), (   0, 255, 160 ), (   0, 255, 192 ), (   0, 255, 224 ),
	(   0, 130, 255 ), (   0, 114, 255 ), (   0, 104, 255 ), (  10,  96, 255 ),
	(   0,  82, 255 ), (   0,  74, 255 ), (   0,  64, 255 ), (   0,  32, 255 ), 
	(   0,   0, 255 ), (  32,   0, 255 ), (  64,   0, 255 ), (  96,   0, 255 ),
	( 128,   0, 255 ), ( 160,   0, 255 ), ( 192,   0, 255 ), ( 224,   0, 255 ),
	( 255,   0, 255 ), ( 255,  32, 255 ), ( 255,  64, 255 ), ( 255,  96, 255 ),
	( 255, 128, 255 ), ( 255, 160, 255 ), ( 255, 192, 255 ), ( 255, 224, 255 ),
	( 255, 255, 255 ), ( 255, 224, 224 ), ( 255, 192, 192 ), ( 255, 160, 160 ),
	( 255, 128, 128 ), ( 255,  96,  96 ), ( 255,  64,  64 ), ( 255,  32,  32 )
)

def pick_palette_color(r, g, b):
	best = -1
	best_index = None
	pplus = ilda_palette_64 + ((0, 0, 0), )
	for i, (rp, gp, bp) in enumerate(pplus):
		err = abs(r - rp) + abs(g - gp) + abs(b - bp)
		if err < best or best < 0:
			best = err
			best_index = i
	if best_index == len(ilda_palette_64):
		return 0x4000
	else:
		return best_index


def rainbow(i):
	def color(c):
		return int(c * color_max + 0.5)
	i *= 3.0
	if i < 1:
		return color(i), 0, color(1 - i)
	elif i < 2:
		return color(2 - i), color(i - 1), 0
	else:
		return 0, color(3 - i), color(i - 2)

def pack_point(mode, x, y, r, g, b):
	if mode in (0, 4):
		out = struct.pack(">hhh", x, y, 0)
	else:
		out = struct.pack(">hh", x, y)

	if mode in (0, 1):
		out += struct.pack(">H", pick_palette_color(r, g, b))
	else:
		out += struct.pack("BBBB", 0, b, g, r)

	return out

def make_circlepoint(mode, i):
	x = int(sin(i*2*pi) * 30000)
	y = int(cos(i*2*pi) * 30000)
	r, g, b = rainbow(i)
	return pack_point(mode, x, y, r, g, b)

def make_frame(mode, npts, framenum, frames):
	ostr = "ILDA" + struct.pack(">I", mode) + "crcltest   j4cbo"
	ostr += struct.pack(">hhhh", npts, framenum, frames, 0)
	return ostr + "".join(
		make_circlepoint(mode, float(i) / npts)
		for i in xrange(npts)
	)

def make_spec():
	for mode in (0, 1, 4, 5):
		for npts in range(50, 500, 5):
			yield mode, npts

frames = list(make_spec())
sys.stderr.write("Producing %d frames\n" % (len(frames)))

for i, (mode, npts) in enumerate(frames):
	sys.stdout.write(make_frame(mode, npts, i + 1, len(frames)))
