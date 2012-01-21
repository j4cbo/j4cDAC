from dfu import dfu, update
import sys
import time

def msg(s):
    print s

def run(argv):
    if len(sys.argv) != 2:
        print "Usage: %s [updatefile]" % (sys.argv[0], )
        sys.exit(1)

    dev = False
    while not dev:
        dev = update.check_device(msg)
        if not dev: time.sleep(1)

    dfu.download(dev, sys.argv[1], msg)

if __name__ == "__main__":
    run(sys.argv)
