PREFIX = arm-none-eabi-

CFLAGS = -Iinc -Ilib/lpc17xx -I../firmware/drivers/include -I../common/inc -fno-common -mcpu=cortex-m3 -mthumb -Os -g -Inet
CFLAGS += -Wall -ffunction-sections -fdata-sections -fshort-enums -gdwarf-2 -gstrict-dwarf -Wl,--gc-sections -nostdlib -nostdinc -Werror
LDFLAGS = -Tlib/lpc17xx/lpc1758.lds -mcpu=cortex-m3 -mthumb -fwhole-program -gdwarf-2 -gstrict-dwarf -Wl,-Map=boot.map -Wl,--gc-sections -nostdlib -nostdinc

# Cortex-M3 support code
CFLAGS += -Ilib/lpc17xx
SRCS += ../common/lib/core_cm3.c lib/lpc17xx/system_LPC17xx.c lib/lpc17xx/startup_LPC17xx.s \
	lib/serial.c ../firmware/drivers/source/lpc17xx_uart.c ../firmware/drivers/source/lpc17xx_clkpwr.c \
	../common/lib/hardware.c

# Mach printf. Much more lightweight than newlib's.
SRCS += ../common/lib/vsnprintf.c minilib.c

# LPCUSB
SRCS += ../common/lib/usbstdreq.c ../common/lib/usbcontrol.c \
	../common/lib/usbhw_lpc.c \
	lib/lpcusb/usbhw_lpc.c lib/lpcusb/usbinit.c

#SRCS += net/ether.c net/network.c net/mdio.c

SRCS += usboot_iap.c main.c enter_app.s crc32.c

OBJS1 = $(SRCS:.c=.boot.o)
OBJS = $(OBJS1:.s=.boot.o)

all: boot.hex size

%.boot.o: %.c
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

%.boot.o: %.s
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

boot.hex: boot.elf
	$(PREFIX)objcopy -O ihex -R .eeprom -R .fuse -R .lock $< $@

boot.elf: $(OBJS)
	$(PREFIX)gcc $(LDFLAGS) $(OBJS) -o boot.elf

.PHONY: clean flash size term

clean:
	rm -f boot.elf boot.hex boot.map $(OBJS)

flash:
	~/lpc21isp/lpc21isp -debug3 boot.hex /dev/ttyUSB0 230400 14746

size:
	$(PREFIX)size -A boot.elf
	$(PREFIX)nm -anS --size-sort -t d boot.elf | grep ^2 | tail -20

term:
	../tools/miniterm/miniterm.py -b 230400 -p /dev/ttyUSB0
