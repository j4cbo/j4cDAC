PREFIX = arm-none-eabi-

CFLAGS = -Iinc -Ilib/core_cm3 -Ilib/lpc17xx -Idrivers/include -fno-common -mcpu=cortex-m3 -mthumb -Os -g
CFLAGS += -Wall -Ilib/lpcusb -fdata-sections
LDFLAGS = -Tlib/lpc17xx/lpc1758.lds -mcpu=cortex-m3 -mthumb -fwhole-program

# Cortex-M3 support code
CFLAGS += -Ilib/core_cm3 -Ilib/lpc17xx
SRCS += lib/core_cm3/core_cm3.c lib/lpc17xx/system_LPC17xx.c lib/lpc17xx/startup_LPC17xx.s \
	drivers/source/lpc17xx_clkpwr.c drivers/source/lpc17xx_pinsel.c \
	drivers/source/lpc17xx_uart.c lib/serial.c main.c

# Mach printf. Much more lightweight than newlib's.
SRCS += lib/minilib/doprnt.c lib/minilib/sprintf.c

# Application
SRCS += lib/dac.c lib/panic.c lib/lightengine.c

# lwIP
LWIP = lwip-1.3.2/src
CFLAGS += -Ilwip-1.3.2/src/include -Ilwip-1.3.2/src/include/ipv4
SRCS += $(LWIP)/core/dns.c $(LWIP)/core/init.c $(LWIP)/core/netif.c \
        $(LWIP)/core/pbuf.c $(LWIP)/core/raw.c $(LWIP)/core/stats.c \
        $(LWIP)/core/sys.c $(LWIP)/core/tcp.c $(LWIP)/core/tcp_in.c \
        $(LWIP)/core/tcp_out.c $(LWIP)/core/udp.c $(LWIP)/core/ipv4/autoip.c \
	$(LWIP)/core/ipv4/icmp.c $(LWIP)/core/ipv4/igmp.c $(LWIP)/core/ipv4/inet.c \
	$(LWIP)/core/ipv4/inet_chksum.c $(LWIP)/core/ipv4/ip_addr.c \
        $(LWIP)/core/ipv4/ip.c $(LWIP)/core/ipv4/ip_frag.c \
        $(LWIP)/netif/etharp.c $(LWIP)/core/dhcp.c

SRCS += lib/skub.c ild-player.c

# Ethernet driver
SRCS += lib/ether.c lib/network.c drivers/source/mdio.c

# FAT
SRCS += lib/fatfs/ff.c lib/sdcard.c lib/fatfs/ccsbcs.c lib/fatfs/diskio.c \
	lib/fatfs/fatfs.c
CFLAGS += -Ilib/fatfs

# HTTP
# SRCS += net/http/httpd.c net/http/fs.c

# LPCUSB
SRCS += lib/lpcusb/usbhw_lpc.c lib/lpcusb/usbinit.c lib/lpcusb/usbstdreq.c \
	lib/lpcusb/usbcontrol.c lib/lpcusb/usbtest.c

SRCS += net/point-stream.c net/broadcast.c net/sink.c net/osc.c net/bpm.c \
	net/ilda-osc.c

OBJS1 = $(SRCS:.c=.o)
OBJS = $(OBJS1:.s=.o)

all: j4cDAC.hex size

%.o: %.c
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

%.o: %.s
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

j4cDAC.hex: j4cDAC.elf
	$(PREFIX)objcopy -O ihex -R .eeprom -R .fuse -R .lock $< $@

j4cDAC.elf: $(OBJS)
	$(PREFIX)gcc $(LDFLAGS) $(OBJS) -o j4cDAC.elf

.PHONY: clean flash size term

clean:
	rm -f j4cDAC.elf $(OBJS)

flash:
	~/lpc21isp/lpc21isp -debug3 j4cDAC.hex /dev/ttyUSB0 230400 14746

size:
	$(PREFIX)size -A j4cDAC.elf
	$(PREFIX)nm -anS --size-sort -t d j4cDAC.elf | grep ^2 | tail -20

term:
	../tools/miniterm/miniterm.py -b 230400 -p /dev/ttyUSB0
