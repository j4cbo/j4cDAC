# ARM stack analyzer, copyright (c) 2007-2011 Jacob Potter.
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2, as published by the Free Software Foundation.

import os
import re

# Parse lines out of objdump.
line_re = re.compile("""
  \\ *			# Leading whitespace
  [a-f0-9]+:\\\t	# Address
  [a-f0-9 ]+\\\t	# Assembled data
  ([a-z.]+)		# Instruction
  [ \t]*		# Gap
  ([^<]*)		# Parameters
  (?:<([^>]+)>)?	# Label, maybe.
""", re.X)

dataline_re = re.compile("""
  \\ *			# Leading whitespace
  [a-f0-9]+:\\\t	# Address
  (
      ((([a-f0-9]{4})|(\\ {4}))\\ ){8}	# Either eight 2-byte values...
    | ((([a-f0-9]{8})|(\\ {8}))\\ ){4}	# ... or four 4-byte values
    | ((([a-f0-9]{2})|(\\ {2}))\\ ){16}	# ... or sixteen bytes
  )
""", re.X)

function_re = re.compile("^[a-f0-9]+ <([^>]+)>:$")

safe_insn_bases = [
	"adc", "add", "adds", "and", "ands", "asr", "asrs",
	"bfi", "bic", "bics", "bkpt",
	"clz", "cmn", "cmp", "cps", "cpsid", "cpsie", "cpy",
	"eor", "eors",
	"it", "ite", "itet", "itete", "itett", "itt", "itte", "ittet", "ittt", "itttt",
	"lsl", "lsls", "lsr", "lsrs",
	"ldrex", "ldrexb", "ldrexh",
	"mla", "mls", "mov", "movs", "movt", "movw", "mrs", "msr", "mul", "muls", "mvn", "mvns",
	"neg", "negs", "nop",
	"orn", "orr", "orrs",
	"rbit", "rev", "rev16", "revsh", "ror", "rsb", "rsbs",
	"sbc", "sbcs", "sdiv", "sev", "strex", "strexb", "strexh", "sub", "subs", "svc", "sxtb", "sxth",
	"teq", "tst",
	"ubfx", "udiv", "umull", "uxtb", "uxth",
	"wfe", "wfi",

	# Not an insn, but whatever
	".word", ".short", ".byte",

	# NOTE: these are only safe because we translate ldmia and stmdb on sp to push and pop.
	"ldmia", "stmdb", "stmia",

	"ldr", "ldrb", "ldrd", "ldrh", "ldrsb", "ldrsh", "str", "strb", "strd", "strh",
]

suffixes = [
	"", "eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"
]

safe_insns = []
for suffix in suffixes:
	safe_insns += [ insn + suffix for insn in safe_insn_bases ]
	safe_insns += [ insn + suffix + ".w" for insn in safe_insn_bases ]
	safe_insns += [ insn + suffix + ".n" for insn in safe_insn_bases ]

class Func(object):
	def __init__(self, name, stacklen, children, tchildren):
		self.name = name
		self.stacklen = stacklen
		self.children = children
		self.tchildren = tchildren

	def __str__(self):
		return "%s (%d%s)" % (self.name, self.stacklen, "" if self.tchildren else "")

	def __repr__(self):
		return "<%s>" % (self.name, )

	def leaf(self):
		return (len(self.children | self.tchildren) == 0)	

	def find_children_recursive(self, namehash, history = []):
		history.append(self)
		terminals = []

		# If this is a leaf function, record the path that got us here
		if self.leaf():
			terminals.append(history)
			return terminals

		# Otherwise, loop through each function this one calls
		for childname in self.children | self.tchildren:
			if childname not in namehash:
				raise Exception("unknown function %s called from %s" % (childname, self.name))
			c = namehash[childname]

			# Detect recursion or loops
			if c in history:
				history.append(c)
				raise Exception("cycle detected: " + str(history))

			terminals += c.find_children_recursive(namehash, list(history))

		return terminals

	def find_children(self, namehash):
		terminals = self.find_children_recursive(namehash, [])
		terminals.sort(key = Func.path_length)
		return terminals

	@staticmethod
	def path_length(fl):
		out = 0
		for i, f in enumerate(fl):
			if f.leaf() or (fl[i+1].name not in f.tchildren):
				out += f.stacklen

		return out


def parse_function(ls):

	function_match = None

	# Read lines until we get a function header
	while True:
		line = ls.next()
		function_match = function_re.match(line)
		if function_match:
			break

	function_name = function_match.group(1)
	callees = set()
	tail_callees = set()
	interesting_lines = []

	# Then, parse each line of it
	while True:
		line = ls.next()

		# Stop when we get something indicating the
		# end of the function
		if line == "\t...":
			continue
		if not line:
			break

		# If this is a data symbol, skip it completely
		if dataline_re.match(line):
			return None

		line_match = line_re.match(line)

		if not line_match:
			print "!!! UNMATCHED: %r" % (line, )
			continue

		# Pull out the instruction and args
		insn, args = line_match.group(1), line_match.group(2)

		# Get rid of comments.
		args = args.split(";")[0].rstrip()

		# Canonicalize push and pop
		if insn == "stmdb" and args.startswith("sp!, "):
			insn = "push"
			args = args[5:]
		if insn == "ldmia.w" and args.startswith("sp!, "):
			insn = "pop"
			args = args[5:]

		# There may or may not be a jump target attached to this insn.
		if line_match.group(3):
			target = line_match.group(3).split("+")[0]
		else:
			target = ""

		# If it's a safe insn, continue
		if insn in safe_insns and args[:2] not in [ "pc", "sp" ]:
			# AAAAAAAAAAAAAAARRRRRRRRRRRRRRRRGH
			if not (insn[:3] in [ "ldr", "str" ] and "sp" in args):
				continue
			elif args.endswith("]"):
				# Can't be an instruction that does writeback
				continue

		# If it's a branch into this function, we're fine
		if target == function_name:
			continue

		# If it's a call, make a note of that
		if insn == "bl" and target:
			callees.add(target)

		else:
			interesting_lines.append((insn, args, target))

	cf = []

	# Walk the interesting lines.
	for op, args, tg in interesting_lines:

		pargs = args.strip("{}").split(", ")

		try:
			operand = int(pargs[1].split()[0][1:])
		except:
			operand = None

		if op == "push":
			cf.append(("stack", 4 * len(pargs)))
		elif op == "pop":
			cf.append(("stack", -4 * len(pargs)))
			if pargs[-1] == 'pc':
				cf.append(("ret", True))
		elif op == "add" and operand:
			cf.append(("stack", -operand))
		elif op == "sub" and operand:
			cf.append(("stack", operand))
		elif op in [ "b.n", "b.w" ] and tg:
			tail_callees.add(tg)
			cf.append(("ret", True))
		elif op in [ "bne.n", "bne.w" ]:
			# XXX: This is a hack. We assume that all non-local
			# branches are conditional tail-calls; if this was
			# supposed to be something else, then the control flow
			# analysis pass will catch it and barf.
			tail_callees.add(tg)
			cf.append(("ret", False))
		elif op == "bx" and args == "lr":
			cf.append(("ret", True))
		elif op.startswith("bx") and args == "lr":
			cf.append(("ret", False))
		elif op in [ "tbb", "tbh" ]:
			# We're pretty sure that there's nothing of import here,
			# but sanity-check...
			cf.append((op, None))
		elif op[:3] in [ "ldr", "str" ] and len(pargs) == 3 \
			and pargs[2][0] == '#':
			off = pargs[2][1:]
			if off.endswith("]!"): off = off[:-2]
			cf.append(("stack", -int(off)))
		elif op == "blx":
			cf.append(("blx", args))
		else:
			cf.append(("unknown", (op, args, tg)))


	# Now, process the control flow.
	peak = 0
	depth = 0
	confused = 0
	for t, v in cf:
		if t == "stack":
			depth += v
			if (depth > peak):
				peak = depth

		elif t == "ret":
			# We should never return with stuff on the stack...
			if depth != 0:
				confused = 1

			# We only understand conditional returns if there
			# was never anything on the stack.
			if peak and (not v):
				confused = 1

			# Reset to our peak depth.
			depth = peak

		elif t in [ "tbb", "tbh" ] and depth:
			# We can be pretty sure that a table jump when we have
			# an active stack frame is local-only.
			pass

		elif t == "blx" and function_name.startswith("FPA_"):
			pass

		else:
			confused = 1

 	# Strcpy is a special case - it was handwritten by ARM and
	# doesn't follow gcc's usual patterns.
	if function_name == "strcpy":
		confused = 0

	if confused:
		print function_name + ": confusing."
		for t, v in cf:
			print "    %s %s" % (t, v)
		print "    " + str(callees)
	else:
		print "%s %d\t%s" % (function_name.ljust(30), peak, " " or callees)

	# If we might tail-call or regular-call, it doesn't count.
	tail_callees -= callees

	if tail_callees:
		print "! TAIL: %s" % (tail_callees, )

	return Func(function_name, peak, callees, tail_callees)


def parse_file(fname):

	# Call objdump on the file to get the disassembled output

	objin, objout = os.popen4("arm-none-eabi-objdump -d -j.text " + fname)
	lines = objout.read().split('\n')
	li = iter(lines)
	fs = {}

	while True:
		try:
			f = parse_function(li)
			if f is None:
				continue
			fs[f.name] = f
		except StopIteration:
			break

	# Wire up function pointers
	for k, v in fs.iteritems():
		if "_FPV_" not in k:
			continue
		src, suffix = k.split("_FPV_")
		if "FPA_" + suffix not in fs:
			print "WARNING: FPA_%s not found" % (suffix, )
			continue

		fs["FPA_" + suffix].children.add(k)

	# Look for setup and mainloop initializers
	iin, iout = os.popen4("grep -Irh ^INITIALIZER .")
	inits = [ t.split('(', 1)[1].strip(' );').split(',')
                  for t in iout.read().split('\n') if ('(' in t) ]
	for group, val in inits:
		group = group.strip(' ')
		val = val.strip(' ')

		if val not in fs:
			print "WARNING: non-linked initializer %s" % (val, )
			continue

		if group in [ "hardware", "protocol" ]:
			fs["FPA_init"].children.add(val)
		elif group == "poll":
			fs["main"].children.add(val)
		else:
			print "WARNING: group %s unknown" % (group, )

	return fs

def grind_tree(funclist):
	# Massive generator expression to sort through everything called
	# from "main" or an interrupt
	functree = (
		(key, funclist[key].find_children(funclist))
		for key
		in sorted(funclist.keys())
		if key == "main" or key.endswith("_Handler") or key.endswith("_IRQHandler")
	)

	return functree

