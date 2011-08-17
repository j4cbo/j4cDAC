#!/usr/bin/python

# j4cDAC abstract generator tester
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

import time
import sys
import BaseHTTPServer

port = 1234

class MyHandler(BaseHTTPServer.BaseHTTPRequestHandler):
	def do_GET(s):
		"""Respond to a GET request."""
		s.send_response(200)
		s.send_header("Content-type", "text/plain")
		s.end_headers()

		if s.path.startswith("/"):
			s.path = s.path[1:]

		pathsec = s.path.rpartition("?")[0].split("/")

		if pathsec[0] != "set":
			return

		print " ".join(pathsec[1:])
		sys.stdout.flush()

	def log_message(fmt, *args, **kwargs):
		pass

if __name__ == '__main__':
	server_class = BaseHTTPServer.HTTPServer
	httpd = server_class(("0.0.0.0", port), MyHandler)
	try:
		httpd.serve_forever()
	except KeyboardInterrupt:
		pass
	httpd.server_close()
