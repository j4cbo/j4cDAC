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

/* Shutter pin config. */
#define DAC_SHUTTER_PIN         6
#define DAC_SHUTTER_EN_PIN      7

enum hw_board_rev hw_board_rev;

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
	} else {
		hw_board_rev = HW_REV_PROTO;
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
	for (i = 0; i < 8; i++) {
		hw_dac_write((i << 12) | (i > 5 ? 0x800 : 0));
	}

	/* Force an LDAC */
	hw_dac_write(0xA002);
}

/* hw_dac_init()
 *
 * Initialize the DAC hardware and sets it to output 0.
 */
void hw_dac_init(void) {

	/* Set up the SPI pins: SCLK, SYNC, DIN. */
	LPC_PINCON->PINSEL0 =
		  (LPC_PINCON->PINSEL0 & ~((3 << 12) | (3 << 14) | (3 << 18)))
		| (2 << 12) | (2 << 14) | (2 << 18);

	/* Turn on the SSP peripheral. */
	LPC_SSP1->CR0 = 0xF | (1 << 6);	/* 16-bit, CPOL = 1; no prescale */
	LPC_SSP1->CR1 = (1 << 1); /* Enable */
	LPC_SSP1->CPSR = 4; /* Divide by 4 -> 24 MHz SPI clock */

	hw_dac_zero_all_channels();

	/* Power up the output buffers */
	hw_dac_write(0xC000);

	/* Set up the shutter output */
	LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_PIN);
	LPC_GPIO2->FIODIR |= (1 << DAC_SHUTTER_PIN);
	LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_EN_PIN);
	LPC_GPIO2->FIODIR |= (1 << DAC_SHUTTER_EN_PIN);
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
