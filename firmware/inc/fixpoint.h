/* Fixed point math utilities
 *
 * Copyright 2011 Dan Mills
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

#ifndef FIXPOINT_INC
#define FIXPOINT_INC

#include <stdint.h>
#include <assert.h>

// 16.16 format fixed point
#define POINT (16)
#define INTEGER (16)
typedef int32_t fixed;

#define FIXED(x) ((fixed)((x) * (1<<POINT)))
#define FLOAT(x) ((float)(x)/(1<<POINT))
#define TRUNC(x) ((x) >> POINT)
 
#define FIX_PI FIXED(3.1415926535)
#define FIX_E FIXED(2.718281828)

// use when one of the arguments is known to be |x|  < FIX(1.0)
static __attribute__((always_inline)) fixed fix_mul_small (const fixed x, const fixed y)
{
  return ((x * y) >> POINT);
}
/// general fixed point multiply 
static __attribute__((always_inline)) fixed fix_mul (const fixed x, const fixed y)
{
  return ((fixed)((((int64_t) x) * y)>> POINT));
}

static __attribute__((always_inline)) fixed fix_div (const fixed numerator, const fixed denom)
{
  ASSERT_NOT_EQUAL(denom, 0);
  return ((((int64_t)numerator)<<POINT)/denom);
}

fixed fix_sqrt (const fixed x);
fixed fix_cordic (const fixed theta, fixed *s, fixed *c);
fixed fix_sine (const uint32_t phase);

#endif
