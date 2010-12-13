#include <LPC17xx.h>
#include <serial.h>
#include "ether.h"
#include <lwip/init.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>

volatile uint32_t time;
volatile uint32_t halfsecond;

#define DEBUG_UART	((LPC_UART_TypeDef *)LPC_UART0)

void SysTick_Handler(void) {
	time++;
	if ((time % 500) == 0) halfsecond = 1;
}

void delay_ms(uint16_t length) {
	uint32_t end = time + length;
	while (time < end);
}

char ether_data[] = {
	0x00, 0x11, 0x22, 0x02, 0x03, 0x04,	// source mac
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	// dest mac
	0x08, 0xFF,				// ethertype
	0x12, 0x34, 0x56, 0x78,
	0x12, 0x34, 0x56, 0x78,
	0x12, 0x34, 0x56, 0x78,
	0x12, 0x34, 0x56, 0x78,
	0xf0, 0x0f, 0xc7, 0xc8 
};
	

int main(int argc, char **argv) {
	time = 0;

	uint32_t last_tcp_tick = 0, last_dhcp_coarse = 0, last_dhcp_fine = 0;

	SysTick_Config(SystemCoreClock / 1000);
	serial_init();

	/* LEDs */
	LPC_GPIO0->FIODIR |= (1 << 0);
	LPC_GPIO1->FIODIR |= (1 << 29);

	__enable_irq();

	outputf("=== j4cDAC ===");

	outputf("lwip_init()");
	lwip_init();

	outputf("eth_init()");
	eth_init();

	outputf("Entering main loop...");

	int status = 0;

	while(1) {
		if (status) {
			LPC_GPIO0->FIOPIN = 1;
			LPC_GPIO1->FIOPIN = 0;
			status = 0;
		} else {
			LPC_GPIO0->FIOPIN = 0;
			LPC_GPIO1->FIOPIN = (1 << 29);
			status = 1;
		}

		if (time > (last_tcp_tick + 250)) {
			outputf("=== TCP TICK ===");
			tcp_tmr();
			last_tcp_tick += 250;
		}

		if (time > (last_dhcp_coarse + 60000)) {
			outputf("=== DHCP COARSE TICK ===");
			dhcp_coarse_tmr();
			last_dhcp_coarse += 60000;
		}

		if (time > (last_dhcp_fine + 500)) {
			outputf("=== DHCP FINE TICK ===");
			dhcp_fine_tmr();
			last_dhcp_fine += 500;
		}

		eth_poll();
	}
}
