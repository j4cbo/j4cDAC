#include <LPC17xx.h>
#include <serial.h>
#include "ether.h"

volatile uint32_t time;

#define DEBUG_UART	((LPC_UART_TypeDef *)LPC_UART0)

void SysTick_Handler(void) {
	time++;
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
	SysTick_Config(SystemCoreClock / 1000);
	serial_init();

	/* LEDs */
	LPC_GPIO0->FIODIR |= (1 << 0);
	LPC_GPIO1->FIODIR |= (1 << 29);

	__enable_irq();

	struct pbuf p;
	p.len = sizeof(ether_data);
	p.tot_len = sizeof(ether_data);
	p.payload = ether_data;
	p.next = 0;
	p.ref = 1;

	outputf("Starting Ethernet...\n");

	eth_init();

	while(1) {
		LPC_GPIO0->FIOPIN = 1;
		LPC_GPIO1->FIOPIN = 0;
		delay_ms(500);
		LPC_GPIO0->FIOPIN = 0;
		LPC_GPIO1->FIOPIN = (1 << 29);
		delay_ms(500);
		outputf("Hello!");
		eth_transmit(0, &p);
	}
}
