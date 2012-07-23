/* j4cDAC useful GCC attributes and macros
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

/* COLD, HOT
 *
 * GCC supports marking of cold and hot functions, for better locality and
 * size.
 */
#define COLD __attribute__((cold))
#define HOT __attribute__((hot))

/* AHB0
 *
 * Put this object in the AHB SRAM block, rather than the main SRAM.
 */
#define AHB0 __attribute__((section(".ahb_sram_0")))

/* NOINLINE, ALWAYS_INLINE
 *
 * Force inlining on or off.
 */
#define NOINLINE __attribute__((noinline))
#define ALWAYS_INLINE __attribute__((always_inline))

/* ARRAY_NELEMS(arr)
 *
 * Return the number of elements in an array.
 */
#define ARRAY_NELEMS(arr)	(sizeof(arr) / sizeof(arr[0]))

/* emergency_exit_0(block)
 * emergency_exit_1(param, block)
 * emergency_exit_2(p1, p2, block)
 *
 * Call block in another function. This is used to defeat an optimization
 * bug in GCC (as of 4.6.3, at least), in which functions with any calls
 * to other functions - even noreturn functions like panic() - will have
 * a significantly overweight and unnecessary stack frame. This can be
 * a performance issue for things like interrupt handlers.
 *
 * These macros fool the compiler into thinking that no call exists,
 * allowing for better code output. Make sure the passed block does not
 * return - calling panic() unconditionally will do fine.
 *
 * Usage example:
 *       emergency_exit_1(state, {
 *               panic("Bogus state %d", state);
 *        });
 */
#define emergency_exit_thunk(line) _emergency_exit_ ## line
#define emergency_exit_thunk2(line) emergency_exit_thunk(line)

#define emergency_exit_0(block) do { 					\
	void __attribute__((used, noreturn))				\
		emergency_exit_thunk2(__LINE__)	(void) { block }	\
	asm volatile("mov pc, %0" : :					\
		"r"(emergency_exit_thunk2(__LINE__)));			\
	__builtin_unreachable(); 					\
	} while(0);

#define emergency_exit_1(p1, block) do { 				\
	void __attribute__((used, noreturn))				\
		emergency_exit_thunk2(__LINE__)			 	\
		(typeof(p1) p1) { block }				\
	asm volatile("mov r0, %0 \n mov pc, %1" : :			\
		"r"(p1), "r"(emergency_exit_thunk2(__LINE__)) : "r0");	\
	__builtin_unreachable(); 					\
	} while(0);

#define emergency_exit_2(p1, p2, block) do {				\
	void __attribute__((used)) emergency_exit_thunk2(__LINE__) 	\
		(typeof(p1) p1, typeof(p2) p2) { block }		\
	asm volatile("mov r0, %0 \n mov r1, %1 \n mov pc, %2" : :	\
		"r"(p1), "r"(p2), "r"(emergency_exit_thunk2(__LINE__))	\
		: "r0", "r1");						\
	__builtin_unreachable(); 					\
	} while(0);

#endif
