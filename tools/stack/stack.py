#!/usr/bin/python2.5
#
# ARM stack analyzer, copyright (c) 2007-2011 Jacob Potter.
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2, as published by the Free Software Foundation.

import sys
import stacklib

MAX_TREES = 15

if len(sys.argv) < 2:
	print "usage: %s [object]" % sys.argv[0]
	sys.exit(1)

known_funcs = stacklib.parse_file(sys.argv[1])
functree = stacklib.grind_tree(known_funcs)

for startfunc, terminals in functree:
	print "Stack used from %s: " % (startfunc, )

	if len(terminals) > MAX_TREES:
		print "\t(%d call paths omitted)" % (len(terminals) - MAX_TREES)

	for funclist in terminals[-MAX_TREES:]:
		pathlist = []
		s = 0

		for i, f in enumerate(funclist):
			tc = (not f.leaf() and funclist[i+1].name in f.tchildren)
			if tc:
				pathlist.append(str(f) + " [TC]")
			else:
				pathlist.append(str(f))
				s += f.stacklen
			
		print "\tTotal %d: %s" % (s, " -> ".join(pathlist))

	print

