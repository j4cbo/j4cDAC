#include <LPC17xx.h>
#include <serial.h>
#include "ether.h"
#include <lwip/init.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>
#include <string.h>
#include <dac.h>

#include <lpc17xx_gpdma.h>
#include <lpc17xx_timer.h>

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

void HardFault_Handler_C(uint32_t *stack) {
	outputf("*** HARD FAULT ***");
	outputf("pc: %p", stack[6]);
	while(1);
}

void BusFault_Handler_C(uint32_t *stack) {
	outputf("*** BUS FAULT ***");
	outputf("pc: %p", stack[6]);
	while(1);
}

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
	dac_init();
/*
	outputf("lwip_init()");
	lwip_init();

	outputf("eth_init()");
	eth_init();

*/
	outputf("Entering main loop...");

	dac_configure(30000);
	int status = 0;
	int ctr = 0;

	while(1) {
		uint16_t * ptr = 0;
		int len = dac_request(&ptr);
		if (len < 0) {
			outputf("*** UNDERFLOW ***");
			dac_configure(30000);
		} else if (len > 0) {
			int i;
			for (i = 0; i < len; i++) {
				ptr[i] = ctr & 0xFFF;
				ctr += 2;
			}
			dac_advance(len);
		}

		if (!(LPC_GPIO1->FIOPIN & (1 << 26))) {
			outputf("Blocking...");
			while (!(LPC_GPIO1->FIOPIN & (1 << 26)));
		}
/*
		outputf("ch0: %p -> %p", LPC_GPDMACH0->DMACCSrcAddr, LPC_GPDMACH0->DMACCDestAddr);
		outputf("dmac: int %x, tc %x, er %x, rawinttc %x", LPC_GPDMA->DMACIntStat, LPC_GPDMA->DMACIntTCStat, LPC_GPDMA->DMACIntErrStat,
			LPC_GPDMA->DMACRawIntTCStat);
		outputf("dmac: rawinterr %x ena %x", LPC_GPDMA->DMACRawIntErrStat, LPC_GPDMA->DMACEnbldChns);
		outputf("t3tc: %x dmaccfg %x ctrl %x", LPC_TIM3->TC, LPC_GPDMACH0->DMACCConfig, LPC_GPDMACH0->DMACCControl);
continue;
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
*/
	}
}
