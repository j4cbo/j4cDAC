from Tkinter import *
from dfu import dfu, update
import thread
import Queue
import time

# Queues for communication between threads
to_ui = Queue.Queue()
from_ui = Queue.Queue()

# Set up the basic window
root = Tk()
root.title("Ether Dream")
root.resizable(FALSE, FALSE)
frame = Frame(root)
frame.grid()
label = Label(frame, text="  ")
label['width'] = 30
label.grid(row=0, column=0, padx=10, pady=20)
go_button = None

def pressed():
    from_ui.put(True)
    go_button.configure(state=DISABLED, text="Updating...")

go_button = Button(frame, text="Update", command=pressed)
go_button.grid(row=1, column=0)

# Copy lines from the update thread into the window
q = Queue.Queue()
def queue_check():
	try:
		while True:
			state, text = to_ui.get_nowait()
			label['text'] = text
			if state:
				go_button.configure(state=NORMAL, text="Update")
			else:
				go_button.configure(state=DISABLED, text="Update")
	except Queue.Empty:
		root.after(200, queue_check)

root.after(100, queue_check)

def msg(s):
	print s
	to_ui.put((False, s))

# USB thread
def usb_thread():
    while True:
        dev = update.check_device(msg)
        if not dev:
            time.sleep(1)
            continue

        to_ui.put((True, "Ready."))

        # Wait for button
        from_ui.get()

        dfu.download(dev, "j4cDAC.bin", msg)

	# Wait for them to ask us to do it again, I guess?
	to_ui.put((True, "Done."))
	from_ui.get()

thread.start_new(usb_thread, ())

root.mainloop()
