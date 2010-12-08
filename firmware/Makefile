PREFIX = arm-none-eabi-

CFLAGS = -I. -Ilib/core_cm3 -Ilib/lpc17xx -fno-common -mcpu=cortex-m3 -mthumb
LDFLAGS = -Tlib/lpc17xx/lpc1758.lds -mcpu=cortex-m3 -mthumb

# Cortex-M3 support code
CFLAGS += -Ilib/core_cm3 -Ilib/lpc17xx
OBJS += lib/core_cm3/core_cm3.o lib/lpc17xx/system_LPC17xx.o lib/lpc17xx/startup_LPC17xx.o

# Application
OBJS += test.o

%.o: %.c
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

%.o: %.s
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

j4cDAC.elf: $(OBJS)
	$(PREFIX)gcc $(LDFLAGS) $(OBJS) -o j4cDAC.elf

.phony: CLEAN

clean:
	rm -f j4cDAC.elf $(OBJS)
