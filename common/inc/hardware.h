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

#ifndef COMMON_HARDWARE_H
#define COMMON_HARDWARE_H

#include <stdint.h>
#include <LPC17xx.h>
#include <LPC17xx_bits.h>

enum hw_board_rev {
	HW_REV_PROTO = 0,
	HW_REV_MP1 = 1,
	HW_REV_MP2 = 2,
};

extern enum hw_board_rev hw_board_rev;

void hw_get_board_rev();
void hw_dac_init(void);
void hw_dac_zero_all_channels(void);
void hw_open_interlock_forever(void) __attribute__((noreturn));

int clock_init(void);

void led_set_frontled(int state);
void led_set_backled(int state);
void led_init(void);

static inline void __attribute__((always_inline, unused))
hw_dac_write(uint16_t word) {
	while (!(LPC_SSP1->SR & SSPnSR_Transmit_Not_Full));
	LPC_SSP1->DR = word;
}

static inline void __attribute__((always_inline, unused))
hw_dac_write32(uint32_t word) {
	while (!(LPC_SSP1->SR & SSPnSR_Transmit_Empty));
	LPC_GPIO0->FIOCLR = (1 << 6);
	LPC_SSP1->DR = word >> 16;
	LPC_SSP1->DR = word & 0xFFFF;
	while ((LPC_SSP1->SR & SSP_SR_BSY));
	LPC_GPIO0->FIOSET = (1 << 6);
}

void watchdog_init(void);
void watchdog_feed(void);

#define FORCE_BOOTLOAD_FLAG	(*(volatile uint32_t *)0x20083FFC)
#define FORCE_BOOTLOAD_VALUE	0xF0ADF0AD

#define PCLKSEL0_INIT_VALUE	PCLK_UART0(PCLK_CCLK) | PCLK_SSP1(PCLK_CCLK)
#define PCLKSEL1_INIT_VALUE	0
#define PCONP_INIT_VALUE	0xE7A884DE

#endif
