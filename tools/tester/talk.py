#!/usr/bin/env python
#
# j4cDAC test code
#
# Copyright 2011 Jacob Potter
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

import dac

class SquarePointStream(object):
	def produce(self):
		pmax = 15600
		pstep = 100
		cmax = 65535
		while True:
			for x in xrange(-pmax, pmax, pstep):
				yield (x, pmax, cmax, 0, 0)
			for y in xrange(pmax, -pmax, -pstep):
				yield (pmax, y, 0, cmax, 0)
			for x in xrange(pmax, -pmax, -pstep):
				yield (x, -pmax, 0, 0, cmax)
			for y in xrange(-pmax, pmax, pstep):
				yield (-pmax, y, cmax, cmax, cmax)

	def __init__(self):
		self.stream = self.produce()

	def read(self, n):
		return [self.stream.next() for i in xrange(n)]

class NullPointStream(object):
	def read(self, n):
		return [(0, 0, 0, 0, 0)] * n

#dac.find_dac()

d = dac.DAC(dac.find_first_dac())

d.play_stream(SquarePointStream())
