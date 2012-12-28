#!/usr/bin/python
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
			state, text, btn = to_ui.get_nowait()
			label['text'] = text
			if state == "quit":
				root.quit()
			elif state:
				go_button.configure(state=NORMAL, text=btn)
			else:
				go_button.configure(state=DISABLED, text=btn)
	except Queue.Empty:
		root.after(200, queue_check)

root.after(100, queue_check)

def msg(s):
	print s
	to_ui.put((False, s, "Update"))

# USB thread
def usb_thread():
    while True:
        dev = update.check_device(msg)
        if not dev:
            time.sleep(1)
            continue

        to_ui.put((True, "Ready.", "Update"))

        # Wait for button
        from_ui.get()

        dfu.download(dev, "j4cDAC.bin", msg)

	to_ui.put((True, "Done.", "Exit"))
	from_ui.get()
	to_ui.put(("quit", None, None))
        

thread.start_new(usb_thread, ())

root.mainloop()
