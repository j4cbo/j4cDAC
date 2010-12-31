/* j4cDAC useful GCC attributes
 *
 * Copyright 2010 Jacob Potter
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

#ifndef ATTRIB_H
#define ATTRIB_H

/* ATTR_VISIBLE
 *
 * This is needed on functions that are called only from asm, to prevent
 * GCC's whole-program optimization from pruning them.
 */
#define ATTR_VISIBLE __attribute__((externally_visible))

/* RV
 *
 * Tell GCC to warn if a function's result is unused.
 */
#define RV __attribute__((warn_unused_result))

/* AHB0
 *
 * Put this object in the AHB SRAM block, rather than the main SRAM.
 */
#define AHB0 __attribute__((section(".ahb_sram_0")))

#endif
