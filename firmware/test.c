#include "LPC17xx.h"

volatile uint32_t time;

void SysTick_Handler(void) {
	time++;
}

void delay_ms(uint16_t length) {
	uint32_t end = time + length;
	while (time < end);
}

int main() {
	SysTick_Config(SystemCoreClock / 1000);

	LPC_GPIO0->FIODIR = 1;

	while(1) {
		LPC_GPIO0->FIOPIN = 1;
		delay_ms(500);
		LPC_GPIO0->FIOPIN = 0;
		delay_ms(500);
	}
}
