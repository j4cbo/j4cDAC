import time
import usb

def look_for_device(msg):
    dev = usb.core.find(idVendor=0xffff, idProduct=0x0005)

    if dev is None:
        msg("No DAC found.")
        return None

    isBoot = (dev[0][(0,0)].bInterfaceClass == 254
              and dev[0][(0,0)].bInterfaceSubClass == 1)

    if not isBoot:
        msg("Resetting...")
        dev.ctrl_transfer(0, 3, 0xf0ad, 0xf0ad)
        time.sleep(.1)
        dev.reset()
        return None

    msg("Ready.")
    return dev

def check_device(msg):
    try:
        return look_for_device(msg)
    except usb.USBError, e:
        return None
