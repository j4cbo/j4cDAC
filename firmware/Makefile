PREFIX = arm-none-eabi-

CFLAGS = -Iinc -Ilib/core_cm3 -Ilib/lpc17xx -Idrivers/include -fno-common -mcpu=cortex-m3 -mthumb -O3
CFLAGS += -Wall
LDFLAGS = -Tlib/lpc17xx/lpc1758.lds -mcpu=cortex-m3 -mthumb

# Cortex-M3 support code
CFLAGS += -Ilib/core_cm3 -Ilib/lpc17xx
OBJS += lib/core_cm3/core_cm3.o lib/lpc17xx/system_LPC17xx.o lib/lpc17xx/startup_LPC17xx.o \
        drivers/source/lpc17xx_uart.o drivers/source/lpc17xx_clkpwr.o \
        drivers/source/lpc17xx_pinsel.o lib/serial.o

# lwIP
LWIP = lwip-1.3.2/src
CFLAGS += -Ilwip-1.3.2/src/include -Ilwip-1.3.2/src/include/ipv4
OBJS += $(LWIP)/core/dhcp.o $(LWIP)/core/dns.o $(LWIP)/core/init.o \
        $(LWIP)/core/mem.o $(LWIP)/core/memp.o $(LWIP)/core/netif.o \
        $(LWIP)/core/pbuf.o $(LWIP)/core/raw.o $(LWIP)/core/stats.o \
        $(LWIP)/core/sys.o $(LWIP)/core/tcp.o $(LWIP)/core/tcp_in.o \
        $(LWIP)/core/tcp_out.o $(LWIP)/core/udp.o $(LWIP)/core/ipv4/autoip.o \
	$(LWIP)/core/ipv4/icmp.o $(LWIP)/core/ipv4/igmp.o $(LWIP)/core/ipv4/inet.o \
	$(LWIP)/core/ipv4/inet_chksum.o $(LWIP)/core/ipv4/ip_addr.o \
        $(LWIP)/core/ipv4/ip.o $(LWIP)/core/ipv4/ip_frag.o \
        $(LWIP)/netif/etharp.o

# Ethernet driver
OBJS += lib/ether.o drivers/source/mdio.o

# Application
OBJS += test.o

%.o: %.c
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

%.o: %.s
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

j4cDAC.hex: j4cDAC.elf
	$(PREFIX)objcopy -O ihex -R .eeprom -R .fuse -R .lock $< $@

j4cDAC.elf: $(OBJS)
	$(PREFIX)gcc $(LDFLAGS) $(OBJS) -o j4cDAC.elf

.PHONY: clean flash

clean:
	rm -f j4cDAC.elf $(OBJS)

flash:
	~/lpc21isp/lpc21isp -debug5 j4cDAC.hex /dev/ttyUSB0 115200 14746
