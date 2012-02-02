import os
import sys
import time
import usb

#
# libusb constants
#

REQUEST_TYPE_CLASS = 0x20
RECIPIENT_INTERFACE = 1
ENDPOINT_OUT = 0
ENDPOINT_IN = 0x80

#
# dfu constants
#

DFU_DETACH = 0
DFU_DNLOAD = 1
DFU_GETSTATUS = 3
DFU_ABORT = 6

DFU_STATUS_OK                 = 0
DFU_STATUS_errTARGET          = 1
DFU_STATUS_errFILE            = 2
DFU_STATUS_errWRITE           = 3
DFU_STATUS_errERASE           = 4
DFU_STATUS_errCHECK_ERASED    = 5
DFU_STATUS_errPROG            = 6
DFU_STATUS_errVERIFY          = 7
DFU_STATUS_errADDRESS         = 8
DFU_STATUS_errNOTDONE         = 9
DFU_STATUS_errFIRMWARE        = 10
DFU_STATUS_errVENDOR          = 11
DFU_STATUS_errUSBR            = 12
DFU_STATUS_errPOR             = 13
DFU_STATUS_errUNKNOWN         = 14
DFU_STATUS_errSTALLEDPKT      = 15

DFU_STATE_appIDLE              = 0
DFU_STATE_appDETACH            = 1
DFU_STATE_dfuIDLE              = 2
DFU_STATE_dfuDNLOAD_SYNC       = 3
DFU_STATE_dfuDNBUSY            = 4
DFU_STATE_dfuDNLOAD_IDLE       = 5
DFU_STATE_dfuMANIFEST_SYNC     = 6
DFU_STATE_dfuMANIFEST          = 7
DFU_STATE_dfuMANIFEST_WAIT_RST = 8
DFU_STATE_dfuUPLOAD_IDLE       = 9
DFU_STATE_dfuERROR             = 10

def dfu_abort(dev, transaction):
    ret = dev.ctrl_transfer(
        ENDPOINT_OUT | REQUEST_TYPE_CLASS | RECIPIENT_INTERFACE,
        DFU_ABORT,
        transaction,
        0,
        ""
    )

def dfu_download(dev, data, transaction):
    ret = dev.ctrl_transfer(
        ENDPOINT_OUT | REQUEST_TYPE_CLASS | RECIPIENT_INTERFACE,
        DFU_DNLOAD,
        transaction,
        0,
        data
    )
    assert ret == len(data)

class DFUStatusResponse:
    def __init__(self, data):
        self.status = data[0]
        self.poll_timeout = (data[3] << 16) | (data[2] << 8) | data[1]
        self.state = data[4]
        self.string = data[5]
    def __repr__(self):
        s = "<DFUStatusResponse: status %s timeout %s state %s string %s>"
        return s % (self.status, self.poll_timeout, self.state, self.string)

def dfu_get_status(dev):
    return DFUStatusResponse(dev.ctrl_transfer(
        ENDPOINT_IN | REQUEST_TYPE_CLASS | RECIPIENT_INTERFACE,
        DFU_GETSTATUS,
        0,
        0,
        6
    ))

class BadFile(Exception):
    pass

def download(dev, filename, msg):
    """download(dev, filename, msg)

    Download filename to device. Use msg to emit status messages.

    Returns True if download was successful; False otherwise.
    """ 
    if "_MEIPASS2" in os.environ:
        filename = os.path.join(os.environ["_MEIPASS2"], filename)
    try:
        file_data = file(filename, "rb").read()
    except:
        msg("Data not found")
        return

    if len(file_data) % 64:
        raise Exception("Invalid firmware image - bad size")

    chunks = [ file_data[i:i+64] for i in range(0, len(file_data), 64) ]

    # Make sure we're in idle state
    st = dfu_get_status(dev)
    if st.state != DFU_STATE_dfuIDLE:
        dfu_abort(dev, 0)
        st = dfu_get_status(dev)
        if st.state != DFU_STATE_dfuIDLE:
            raise Exception("Could not return device to idle state")

    # Write the file
    for i, data in enumerate(chunks):
        msg("Loading: %d%% (%d)" % (100 * i / len(chunks), i))
        dfu_download(dev, data, i)

        while True:
            st = dfu_get_status(dev)
            if st.state in (DFU_STATE_dfuDNLOAD_IDLE, DFU_STATE_dfuERROR):
                break

            time.sleep(st.poll_timeout / 1000.0)

        if st.status != DFU_STATUS_OK:
            print st
            return False

    print st
    msg("Loading: 100%")

    # Send final 0-byte data chunk
    dfu_download(dev, "", 0)

    msg("Loading: done")

    # Wait until no longer in MANIFEST or MANIFEST_SYNC state
    while True:
        st = dfu_get_status(dev)
        print st

        time.sleep(st.poll_timeout / 1000.0)

        if st.state in (DFU_STATE_dfuMANIFEST_SYNC, DFU_STATE_dfuMANIFEST):
            time.sleep(1)
            continue

        break

    global done
    done = 1

    # Reset
    ret = dev.ctrl_transfer(
        ENDPOINT_OUT | REQUEST_TYPE_CLASS | RECIPIENT_INTERFACE,
        DFU_DETACH,
        1000,
        0,
        ""
    )

    # If this breaks, whatever, we're done.
    try:
        dev.reset()
    except usb.USBError:
        pass

