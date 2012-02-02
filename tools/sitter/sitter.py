#!/usr/bin/python
# j4cDAC "sitter"
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

import socket
import time
import struct

def pack_point(x, y, r, g, b, i = -1, u1 = 0, u2 = 0, flags = 0):
	"""Pack some color values into a struct dac_point.

	Values must be specified for x, y, r, g, and b. If a value is not
	passed in for the other fields, i will default to max(r, g, b); the 
	rest default to zero.
	"""
	
	if i < 0:
		i = max(r, g, b)

	return struct.pack("<HhhHHHHHH", flags, x, y, i, r, g, b, u1, u2)


class ProtocolError(Exception):
	"""Exception used when a protocol error is detected."""
	pass


class Status(object):
	"""Represents a status response from the DAC."""

	def __init__(self, data):
		"""Initialize from a chunk of data."""
		self.protocol_version, self.le_state, self.playback_state, \
		  self.source, self.le_flags, self.playback_flags, \
		  self.source_flags, self.fullness, self.point_rate, \
		  self.point_count = \
			struct.unpack("<BBBBHHHHII", data)

	def dump(self, prefix = " - "):
		"""Dump to a string."""
		lines = [
			"Light engine: state %d, flags 0x%x" %
				(self.le_state, self.le_flags),
			"Playback: state %d, flags 0x%x" %
				(self.playback_state, self.playback_flags),
			"Buffer: %d points" %
				(self.fullness, ),
			"Playback: %d kpps, %d points played" %
				(self.point_rate, self.point_count),
			"Source: %d, flags 0x%x" %
				(self.source, self.source_flags)
		]
		for l in lines:
			print prefix + l


class BroadcastPacket(object):
	"""Represents a broadcast packet from the DAC."""

	def __init__(self, st, ip=None):
		"""Initialize from a chunk of data."""
		self.mac = st[:6]
		self.hw_rev, self.sw_rev, self.buffer_capacity, \
		self.max_point_rate = struct.unpack("<HHHI", st[6:16])
		self.status = Status(st[16:36])
		self.ip = ip

	def dump(self, prefix = " - "):
		"""Dump to a string."""
		lines = [
			"MAC: " + ":".join(
				"%02x" % (ord(o), ) for o in self.mac),
			"HW %d, SW %d" %
				(self.hw_rev, self.sw_rev),
			"Capabilities: max %d points, %d kpps" %
				(self.buffer_capacity, self.max_point_rate)
		]
		for l in lines:
			print prefix + l
		self.status.dump(prefix)

	def macstr(self):
		return "".join("%02x" % (ord(c), ) for c in self.mac)


class DAC(object):
	"""A connection to a DAC."""

	def got_broadcast(self, bp):
		self.last_broadcast = bp
		self.last_broadcast_time = time.time()

	def read(self, l):
		"""Read exactly length bytes from the connection."""
		while l > len(self.buf):
			self.buf += self.conn.recv(4096)

		obuf = self.buf
		self.buf = obuf[l:]
		return obuf[:l]

	def readresp(self, cmd):
		"""Read a response from the DAC."""
		data = self.read(22)
		response = data[0]
		cmdR = data[1]
		status = Status(data[2:])

#		status.dump()

		if cmdR != cmd:
			raise ProtocolError("expected resp for %r, got %r"
				% (cmd, cmdR))

		if response != "a":
			raise ProtocolError("expected ACK, got %r"
				% (response, ))

		self.last_status = status
		return status

	def __init__(self, macstr, bp):
		self.macstr = macstr
		self.firmware_string = "-"
		self.got_broadcast(bp)

		try:
			t1 = time.time()
			self.connect(self.last_broadcast.ip[0])
			t = time.time() - t1
			self.conn_status = "ok (%d ms)" % (t * 500)

			if self.last_broadcast.sw_rev < 2:
				self.firmware_string = "(old)"
			else:
				self.conn.sendall('v')
				self.firmware_string = self.read(32).replace("\x00", " ").strip()
		except Exception, e:
			self.conn_status = str(e)

	def connect(self, host, port = 7765):
		"""Connect to the DAC over TCP."""
		conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		conn.settimeout(0.2)
		conn.connect((host, port))
		self.conn = conn
		self.buf = ""

		# Read the "hello" message
		first_status = self.readresp("?")
		first_status.dump()



	def begin(self, lwm, rate):
		cmd = struct.pack("<cHI", "b", lwm, rate)
		self.conn.sendall(cmd)
		return self.readresp("b")

	def update(self, lwm, rate):
		cmd = struct.pack("<cHI", "u", lwm, rate)
		self.conn.sendall(cmd)
		return self.readresp("u")

	def encode_point(self, point):
		return pack_point(*point)

	def write(self, points):
		epoints = map(self.encode_point, points)
		cmd = struct.pack("<cH", "d", len(epoints))
		self.conn.sendall(cmd + "".join(epoints))
		return self.readresp("d")

	def prepare(self):
		self.conn.sendall("p")
		return self.readresp("p")

	def stop(self):
		self.conn.sendall("s")
		return self.readresp("s")

	def estop(self):
		self.conn.sendall("\xFF")
		return self.readresp("\xFF")

	def clear_estop(self):
		self.conn.sendall("c")
		return self.readresp("c")

	def ping(self):
		self.conn.sendall("?")
		return self.readresp("?")

	def play_stream(self, stream):
		# First, prepare the stream
		if self.last_status.playback_state == 2:
			raise Exception("already playing?!")
		elif self.last_status.playback_state == 0:
			self.prepare()

		started = 0

		while True:
			# How much room?
			cap = 1799 - self.last_status.fullness
			points = stream.read(cap)

			if cap < 100:
				time.sleep(0.005)
				cap += 150

#			print "Writing %d points" % (cap, )
			t0 = time.time()
			self.write(points)
			t1 = time.time()
#			print "Took %f" % (t1 - t0, )

			if not started:
				self.begin(0, 30000)
				started = 1


def find_dac():
	"""Listen for broadcast packets."""

	s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	s.bind(("0.0.0.0", 7654))

	while True:
		data, addr = s.recvfrom(1024)
		bp = BroadcastPacket(data)
		
		print "Packet from %s: " % (addr, )
		bp.dump()


from Tkinter import *
import socket
import Queue
import thread
import time

class DacDisplay(LabelFrame):
	def __init__(self, master):
		LabelFrame.__init__(self, master, width=600, height=400)
		self.grid_propagate(0)

		Label(self, text="IP Address:").grid(row=0, column=0, sticky=N+E)
		Label(self, text="Version:").grid(row=1, column=0, sticky=N+E)
		Label(self, text="Status:").grid(row=2, column=0, sticky=N+E)
		Label(self, text="Source:").grid(row=3, column=0, sticky=N+E)
		Label(self, text="Network:").grid(row=4, column=0, sticky=N+E)
		Label(self, text="Firmware:").grid(row=5, column=0, sticky=N+E)

		self.iplabel = Label(self, text = "")
		self.iplabel.grid(row=0, column=1, sticky=N+W)
		self.verslabel = Label(self, text = "")
		self.verslabel.grid(row=1, column=1, sticky=N+W)
		self.stlabel = Label(self, text = "")
		self.stlabel.grid(row=2, column=1, sticky=N+W) 
		self.srclabel = Label(self, text = "")
		self.srclabel.grid(row=3, column=1, sticky=N+W) 
		self.netlabel = Label(self, text = "")
		self.netlabel.grid(row=4, column=1, sticky=N+W) 
		self.fwlabel = Label(self, text = "")
		self.fwlabel.grid(row=5, column=1, sticky=N+W) 

		self.display_none()

	def display_none(self):
		self['text'] = "Ether Dream"

		for l in self.iplabel, self.verslabel, self.stlabel, self.srclabel, self.netlabel, self.fwlabel:
			l['text'] = ""

	def display_dac(self, dac):
		b = dac.last_broadcast
		self['text'] = "Ether Dream " + b.macstr()[6:]
		self.iplabel['text'] = str(b.ip[0])
		self.verslabel['text'] = "hardware %d, software %d" % (b.hw_rev, b.sw_rev)

		st_str = ""
		if b.status.le_state == 0:
			st_str = "online, "
		else:
			st_str = "ESTOP ACTIVE (%d), " % (b.status.le_flags, )

		if b.status.playback_state == 0:
			st_str += "idle"
		elif b.status.playback_state == 1:
			st_str += "prepared"
		elif b.status.playback_state == 2:
			st_str += "playing (%d buffered, %d played)" % (b.status.fullness, b.status.point_count)
		else:
			st_str += "DAC state %d" % (b.status.playback_state, )

		if b.status.point_rate:
			st_str += ", %d pps" % (b.status.point_rate)

		self.stlabel['text'] = st_str

		if b.status.source == 0:
			src_str = "network"
		elif b.status.source == 1:
			src_str = "file playback: %s, repeat %s" % (
				b.status.source_flags & 1 and "playing" or "not playing",
				b.status.source_flags & 2 and "on" or "off"
			)
		elif b.status.source == 2:
			src_str = "abstract generator: %s" % (
				b.status.source_flags & 1 and "playing" or "not playing",
			)
		else:
			src_str = "unknown %d" % (b.status.source)
			
		self.srclabel['text'] = src_str
		self.netlabel['text'] = dac.conn_status
		self.fwlabel['text'] = dac.firmware_string

class DacTracker(Listbox):
	def __init__(self, master, *args, **kwargs):
		Listbox.__init__(self, master, *args, **kwargs)

		self.dac_list = []
		self.dac_macstr_map = {}

		self.bind("<<ListboxSelect>>", self.update_selection)

	def update_selection(self, lb=None):
		if self.dac_display:
			try:
				dac_obj = self.dac_list[self.index(ACTIVE)]
			except:
				return
			self.dac_display.display_dac(dac_obj)

	def got_packet(self, bp):
		macstr = bp.macstr()
		if macstr not in self.dac_macstr_map:
			new_dac = DAC(macstr, bp)
			self.insert(END, macstr[6:])
			self.dac_list.append(new_dac)
			self.dac_macstr_map[macstr] = new_dac
			dac_obj = new_dac
		else:
			dac_obj = self.dac_macstr_map[macstr]
			dac_obj.got_broadcast(bp)

		if len(self.dac_list) == 1:
			self.selection_set(0)
			self.update_selection()

		def check_on_dac():
			if time.time() - dac_obj.last_broadcast_time < 2:
				return

			idx = self.dac_list.index(dac_obj)
			self.dac_list.remove(dac_obj)
			del self.dac_macstr_map[macstr]
			self.delete(idx)
			self.dac_display.display_none()


		self.after(2000, check_on_dac)



# Set up the basic window
root = Tk()
root.title("Ether Dream")
root.resizable(FALSE, FALSE)
frame = Frame(root)
frame.grid()

disp = DacDisplay(root)
disp.grid(row=0, column=1, padx=5, pady=5)
tracker = DacTracker(root, height=22)
tracker.grid(row=0, column=0, padx=5, pady=5)
tracker.dac_display = disp

# Set up queue checker
packet_queue = Queue.Queue()
def queue_check():
	try:
		while True:
			data, addr = packet_queue.get_nowait()
			tracker.got_packet(BroadcastPacket(data, addr))
	except Queue.Empty:
		root.after(100, queue_check)

root.after(100, queue_check)

# Set up listening socket and thread
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 7654))
def socket_thread():
	while True:
		packet_queue.put(s.recvfrom(1024))
thread.start_new(socket_thread, ())

root.mainloop()
