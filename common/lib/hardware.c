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
#include <LPC17xx.h>

enum hw_board_rev hw_get_board_rev(void) {

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
		return HW_REV_MP1;
	} else {
		return HW_REV_PROTO;
	}
}
