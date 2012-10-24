/* j4cDAC common hardware functions
 *
 * Copyright 2011 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or 3, or the GNU Lesser General Public License version 3, as published
 * by the Free Software Foundation, at your option.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <hardware.h>
#include <stdint.h>
#include <LPC17xx.h>
#include <serial.h>

uint32_t SystemCoreClock = 4000000;

/* Shutter pin config. */
#define DAC_SHUTTER_PIN         6
#define DAC_SHUTTER_EN_PIN      7

enum hw_board_rev hw_board_rev;
char hw_dac_16bit;

void hw_get_board_rev(void) {

	/* On rev. 0 hardware, P1[31] is connected to P2[10]. In rev. 1,
	 * P1[31] is wired to PHY_RESET instead. This means that we have
	 * an easy algorithm to distinguish them:
	 * - Configure P2[10] as input with weak pull-up, as it is on reset
	 * - Drive P1[31] low. This resets the PHY. It also pulls P2[10] low
	 *   on rev. 0 boards.
	 */

	/* Enable pull-up on P1[31] */
	LPC_PINCON->PINMODE3 &= ~(3 << 30);
	/* Disable pull-up on P2[10] */
	LPC_PINCON->PINMODE4 = (LPC_PINCON->PINMODE4 & ~(3 << 20))
	                     | (2 << 20);

	/* Drive P1[31] low */
	LPC_GPIO1->FIODIR |= (1 << 31);
	LPC_GPIO1->FIOCLR = (1 << 31);

	/* P2[10] low = rev. 0 board. P2[10] high = MP board. */
	if (LPC_GPIO2->FIOPIN & (1 << 10)) {
		hw_board_rev = HW_REV_MP1;
		/* Put P1[31] back */
		LPC_GPIO1->FIODIR &= ~(1 << 31);

		/* Pull-up or pull-down on interlock pin indicates MP2 */
		if (LPC_GPIO2->FIOPIN & (1 << 8))
			hw_board_rev = HW_REV_MP1;
		else
			hw_board_rev = HW_REV_MP2;
	} else {
		hw_board_rev = HW_REV_PROTO;
	}

	if (hw_board_rev != HW_REV_PROTO) {
		/* Turn on interlock output */
		LPC_GPIO2->FIOSET = (1 << 8);
		LPC_GPIO2->FIODIR |= (1 << 8);
	}
}

/* hw_dac_zero_all_channels()
 *
 * Set all the DAC channels to zero except for X/Y, which default to 0x800
 * (center).
 *
 * Channels 6 and 7 are the galvo axes on both hardware revs.
 *
 */
void hw_dac_zero_all_channels(void) {
	int i;

	if (hw_dac_16bit) {
		for (i = 0; i < 8; i++) {
			int initializer = (i == 1 || i == 3)
				? (0x8000 << 4) : 0;
			hw_dac_write32((3 << 24) | (i << 20) | initializer);
		}
	} else {
		for (i = 0; i < 8; i++) {
			hw_dac_write((i << 12) | (i > 5 ? 0x800 : 0));
		}

		/* Force an LDAC */
		hw_dac_write(0xA002);
	}

	/* Close shutter */
	LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_PIN);

	led_set_backled(0);
}

/* hw_dac_init()
 *
 * Initialize the DAC hardware and sets it to output 0.
 */
void hw_dac_init(void) {
	LPC_PINCON->PINSEL0 &= ~(3 << 12);
	LPC_GPIO0->FIODIR &= ~(1 << 6);

	/* Pull-down on /SYNC indicates 16-bit DAC */
	if (!(LPC_GPIO0->FIOPIN & (1 << 6))) {
		hw_dac_16bit = 1;
		/* Set up the SPI pins: SCLK, DIN. */
		LPC_PINCON->PINSEL0 =
			  (LPC_PINCON->PINSEL0 & ~((3 << 14) | (3 << 18)))
			| (2 << 14) | (2 << 18);
		LPC_GPIO0->FIOSET = (1 << 6);
		LPC_GPIO0->FIODIR |= (1 << 6) | (1 << 9);
	} else {
		hw_dac_16bit = 0;
		/* Drive SYNC from the SPI peripheral also. */
		LPC_PINCON->PINSEL0 =
			  (LPC_PINCON->PINSEL0 & ~((3 << 14) | (3 << 18)))
			| (2 << 12) | (2 << 14) | (2 << 18);
	}

	if (hw_dac_16bit) {
		LPC_PINCON->PINSEL0 &= ~(3 << 12);
	}

	/* Turn on the SSP peripheral. */
	LPC_SSP1->CR0 = 0xF | (1 << 6);	/* 16-bit, CPOL = 1; no prescale */
	LPC_SSP1->CR1 = (1 << 1); /* Enable */

	if (hw_dac_16bit) {
		LPC_SSP1->CPSR = 4; /* 32 MHz SPI clock */
	} else {
		LPC_SSP1->CPSR = 4; /* 24 MHz SPI clock */
	}

	hw_dac_zero_all_channels();

	if (!hw_dac_16bit) {
		/* Set Vref in buffered mode */
		hw_dac_write(0x800C);

		/* Power up the output buffers */
		hw_dac_write(0xC000);
	}

	/* Set up the shutter output */
	LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_PIN);
	LPC_GPIO2->FIODIR |= (1 << DAC_SHUTTER_PIN);
	LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_EN_PIN);
	LPC_GPIO2->FIODIR |= (1 << DAC_SHUTTER_EN_PIN);

	if (hw_dac_16bit) {
		outputf("  16-bit DAC");
	} else {
		outputf("  12-bit DAC");
	}
}

/* led_set_frontled()
 *
 * Set the state of the front (top, on proto) LED.
 */
void led_set_frontled(int state) {
	if (hw_board_rev == HW_REV_PROTO) {
		if (state) LPC_GPIO1->FIOSET = (1 << 29);
		else LPC_GPIO1->FIOCLR = (1 << 29);
	} else {
		if (state) LPC_GPIO0->FIOSET = (1 << 22);
		else LPC_GPIO0->FIOCLR = (1 << 22);
	}
}

/* led_set_backled()
 *
 * Set the state of the back (bottom, on proto) LED.
 */
void led_set_backled(int state) {
	if (hw_board_rev == HW_REV_PROTO) {
		if (state) LPC_GPIO0->FIOSET = (1 << 0);
		else LPC_GPIO0->FIOCLR = (1 << 0);
	} else {
		if (state) LPC_GPIO2->FIOSET = (1 << 5);
		else LPC_GPIO2->FIOCLR = (1 << 5);
	}
}

/* led_init()
 *
 * Initialize the LEDs.
 */
void led_init(void) {
	if (hw_board_rev == HW_REV_PROTO) {
		LPC_GPIO1->FIODIR |= (1 << 29);
		LPC_GPIO0->FIODIR |= (1 << 0);
	} else {
		LPC_GPIO0->FIODIR |= (1 << 22);
		LPC_GPIO2->FIODIR |= (1 << 5);
	}
}

#define PLL_M			48
#define PLL_N			5
#define PLL0CFG_Val		(((PLL_N - 1) << 16) | (PLL_M - 1))
#define CLOCK_TIMEOUT		50000

/* clock_init()
 *
 * Initialize the external clock and PLL.
 */
int clock_init(void) {
	/* Start up main oscillator */
	LPC_SC->SCS = SCS_OSCRANGE | SCS_OSCEN;

	int i = CLOCK_TIMEOUT;
	while (i-- && !(LPC_SC->SCS & SCS_OSCSTAT));
	if (!i) return -1;

	/* Flash accelerator config */
	LPC_SC->FLASHCFG = 0x403A;

	/* Divide pllclk by 5 (4 + 1) to produce CPU clock */
	LPC_SC->CCLKCFG = 4;

	/* Configure PLL0 */
	LPC_SC->PLL0CFG = (((PLL_N - 1) << 16) | (PLL_M - 1));
	LPC_SC->PLL0FEED = 0xAA;
	LPC_SC->PLL0FEED = 0x55;
	LPC_SC->PLL0CON = PLLnCON_Enable;
	LPC_SC->PLL0FEED = 0xAA;
	LPC_SC->PLL0FEED = 0x55;

	/* Wait for lock */
	i = CLOCK_TIMEOUT;
	while (i-- && !(LPC_SC->PLL0STAT & (1<<26)));
	if (!i) return -1;

	/* Set clock source as PLL0 */
	LPC_SC->CLKSRCSEL = 1;

	/* Connect PLL0 */
	LPC_SC->PLL0CON = PLLnCON_Enable | PLLnCON_Connect;
	LPC_SC->PLL0FEED = 0xAA;
	LPC_SC->PLL0FEED = 0x55;

	/* Wait for connect to go through */
	while (i-- && !(LPC_SC->PLL0STAT & ((1<<25) | (1<<24))));
	if (!i) return -1;

	/* pllck is 480 MHz, so divide by 10 to get USB clock */
	LPC_SC->USBCLKCFG = 9;

	/* No CLKOUT */
	LPC_SC->CLKOUTCFG = 0;

	SystemCoreClock = 96000000;

	return 0;
}

/* Open the interlock and then stop forever.
 *
 * This is intended to be called at the end of panic() or similar.
 */
void __attribute__((noreturn)) hw_open_interlock_forever(void) {

	/* Turn off all DAC channels. */
	hw_dac_init();

	if (hw_board_rev == HW_REV_MP1) {
		/* Square wave */
		int i;
		LPC_GPIO2->FIODIR |= (1 << 8);
		while (1) {
			for (i = 0; i < 1000; i++) __NOP();
			LPC_GPIO2->FIOSET = (1 << 8);
			watchdog_feed();
			for (i = 0; i < 1000; i++) __NOP();
			LPC_GPIO2->FIOCLR = (1 << 8);
			watchdog_feed();
		}
	} else {
		/* Just do nothing */
		while (1);
	}
}

#define WDT_MILLISECONDS	20
#define WDT_RC_FREQ		4000000

/* Set up the watchdog timer.
 *
 * Once this is called, watchdog_feed() must be called at least once
 * every WDT_MILLISECONDS ms, or the system will reset.
 */
void watchdog_init(void) {
	LPC_WDT->WDTC = (WDT_MILLISECONDS * WDT_RC_FREQ) / 1000;
	LPC_WDT->WDMOD = (WDMOD_WDEN | WDMOD_WDRESET);
	/* WD clock source defaults to 0, the RC oscillator. Keep that and
	 * lock the input. */
	LPC_WDT->WDCLKSEL = WDSEL_WDLOCK;
}

/* Feed the watchdog.
 *
 * Om nom nom.
 */
void watchdog_feed(void) {
	LPC_WDT->WDFEED = 0xAA;
	LPC_WDT->WDFEED = 0x55;
}
