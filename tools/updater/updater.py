import wx
import usb

class MainWindow(wx.Frame):

	def __init__(self, parent, title):
		wx.Frame.__init__(self, parent, title=title, size=(300, 180),
			style=wx.MINIMIZE_BOX | wx.SYSTEM_MENU | wx.CAPTION | wx.CLOSE_BOX)

		# Set up menu
		filemenu = wx.Menu()
		openitem = filemenu.Append(wx.ID_OPEN, "&Open...")
		self.Bind(wx.EVT_MENU, self.openFile, openitem)
		filemenu.AppendSeparator()
		exititem = filemenu.Append(wx.ID_EXIT,"E&xit")
		self.Bind(wx.EVT_MENU, lambda x: wx.Exit(), exititem)
		menuBar = wx.MenuBar()
		menuBar.Append(filemenu,"&File")
		self.SetMenuBar(menuBar)
		self.Show(True)

		# Set up toolbar
		self.toolbar = self.CreateToolBar()
		openbtn = self.toolbar.AddLabelTool(1, "Open", wx.Bitmap('images/open.png'))
		self.Bind(wx.EVT_TOOL, self.openFile, openbtn)
		dlbtn = self.toolbar.AddLabelTool(2, "Program", wx.Bitmap('images/dnload.png'))
		self.Bind(wx.EVT_TOOL, self.program, dlbtn)
		self.toolbar.Realize()

		# Display some text
		panel = wx.Panel(self, -1)
		panel.Centre()
		self.label = wx.StaticText(panel, -1, "", style=wx.ALIGN_CENTRE)
		self.label.Centre()
		panel.Centre()

		# Timer
		self.timer = wx.Timer(self, 0)
		self.Bind(wx.EVT_TIMER, self.onTimer, id=0)
		self.timer.Start(1000)

		self.count = 0

	def disp(self, s):
		self.label.SetLabel(s)
		self.label.Centre()

	def openFile(self, *args, **kwargs):
		dialog = wx.FileDialog(
			None, message = "Firmware image",
			wildcard = "Firmware images (*.bin)|*.bin", style = wx.OPEN)
		
		dialog.ShowModal()
		print "got %r" % (dialog.GetPath())
		dialog.Destroy()

	def program(self, *args, **kwargs):
		pass

	def onTimer(self, *args, **kwargs):
		self.ready = False
		dev = usb.core.find(idVendor=0xffff, idProduct=0x0005)
		if dev is None:
			self.disp("No DAC.")
			return

		isBoot = False
		try:
			print dev[0][(0,0)].bInterfaceClass
			if (dev[0][(0,0)].bInterfaceClass == 254
			    and dev[0][(0,0)].bInterfaceSubClass == 1):
				isBoot = True
		except:
			pass

		if not isBoot:
			try:
				self.disp("Resetting...")
				dev.ctrl_transfer(0, 3, 0xf0ad, 0xf0ad)
			except Exception, e:
				print repr(e)
			dev.reset()
			return

		self.disp("Ready.")
		self.ready = True

app = wx.App(False)
frame = MainWindow(None, "Ether Dream")
app.MainLoop()
