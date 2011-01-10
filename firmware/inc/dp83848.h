/* j4cDAC DP83848 bit definitions
 *
 * Copyright 2011 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DP83848_H
#define DP83848_H

#define DP83848_BMCR		0x00
#define DP83848_BMSR		0x01
#define DP83848_IDR1		0x02
#define DP83848_IDR2		0x03
#define DP83848_ANAR		0x04
#define DP83848_ANLPAR		0x05
#define DP83848_ANLPARNP	0x06
#define DP83848_ANNPTR		0x07
#define DP83848_PHYSTS		0x10
#define DP83848_MICR		0x11
#define DP83848_MISR		0x12
#define DP83848_FCSCR		0x14
#define DP83848_RECR		0x15
#define DP83848_PCSR		0x16
#define DP83848_RBR		0x17
#define DP83848_LEDCR		0x18
#define DP83848_PHYCR		0x19
#define DP83848_10BTSCR		0x1A
#define DP83848_CDCTRL1		0x1B
#define DP83848_EDCR		0x1D

#endif
