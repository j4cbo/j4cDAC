 /* j4cDAC
  *
  * Copyright 2010, 2011 Jacob Potter
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

.syntax unified

#include <dac.h>
#include <transform.h>

.section .data
.global PWM1_IRQHandler
.align 2
PWM1_IRQHandler:

#if DAC_INSTRUMENT_TIME
	/* Load SysTick */			/*	r0	r1	r2	r3	*/
	movw r0, 0xE018				/*	&time				*/
	movt r0, 0xE000				/*	&time				*/
	ldr r3, [r0]				/*				time	*/
#endif

	/* Get dac_control ready */
	movw r0, :lower16:(dac_control+20)	/*	&c			time	*/
	movt r0, :upper16:(dac_control+20)	/*	&c			time	*/

	/* Load irq_do flag */
	ldrb r2, [r0, -12]			/*	&c		do	time	*/

	/* Check what we're supposed to do */
	cmp r2, 14				/*	&c		do=14	time	*/
	bne .L.goto_dac_handle_abstract

	/* Load buf produce/consume pointers */
	ldr r1, [r0, -16]			/*	&c	p/c	14	time						*/
	uxth ip, r1				/*	&c	p/c	14	time					prod	*/
	uxth r1, r1, ror 16			/*	&c	cons	14	time					prod	*/

	/* Underflow? */
	cmp r1, ip
	beq .L.goto_dac_stop_underflow

	/* Save some registers */		/*	[r0]	[r1]	[r2]	[r3]	[r4]	[r5]	[r6]	[r7]	*/
	push { r3, r4, r5, r6, r7 }		/*	&c	cons	14						*/

	/* Find the address of our point */
	movw r4, :lower16:dac_buffer		/*	&c	cons	14		buf0				*/
	movt r4, :upper16:dac_buffer		/*	&c	cons	14		buf0				*/
	mla r5, r2, r1, r4			/*	&c	cons				point			*/	

	/* Clear PWM1 IRQ */
	movw r4, 0x8000				/*	&c	cons			&irq	point			*/
	movt r4, 0x4001				/*	&c	cons			&irq	point			*/
	mov r3, 1				/*	&c	cons		1	&irq	point			*/
	str r3, [r4]				/*	&c	cons		1		point			*/

	/* Increment counter */
	ldr r2, [r0, -20]			/*	&c	cons	count	1		point			*/
	add r2, r3				/*	&c	cons	cnt+1	1		point			*/

	/* Increment and writeback */
	add r1, r3				/*	&c	c+1	cnt+1	1		point			*/
	cmp r1, DAC_BUFFER_POINTS		/*	&c	c+1	cnt+1			point			*/
	str r2, [r0, -20]			/*	&c	c+1				point			*/
	it ge					/*	&c	c+1				point			*/
	movge r1, 0				/*	&c	cons				point			*/

	/* Time to handle blue+flags */
	ldrh r2, [r5, 12]			/*	&c		bf			point			*/

	tst r2, (1<<15)
	strh r1, [r0, -14]			/*	&c					point			*/
	bne .L.goto_dac_pop_rate_change
.L.back_from_pop_rate_change:

	/* Load/increment produce and consume pointers */
	ldr r7, [r0, 32]			/*	&c		bf			point		dlc	*/
	add r7, 0x01010101			/*	&c		bf			point		dlc	*/
	and r7, 0x0f0f0f0f			/*	&c		bf			point		dlc	*/
	str r7, [r0, 32]			/*	&c		bf			point		dlc	*/

	/* Extract blue produce pointer	*/
	uxtb r6, r7				/*	&c		bf			point	bprod	dlc	*/

	/* Produce into blue delay line */
	ubfx r2, r2, 0, 12			/*	&c		blue			point	bprod	dlc	*/

	/* Load up DR */
	movw r4, 0x0008				/*	&c		blue		DR	point	bprod	dlc	*/
	movt r4, 0x4003				/*	&c		blue		DR	point	bprod	dlc	*/
	strh r2, [r0, r6, lsl 1]		/*	&c					point		dlc	*/

	/* Now, load the rest of the point */
	ldr r1, [r5]
	ldr r2, [r5, 4]
	ldr r3, [r5, 8]

	/* Output U2 */
	ubfx r5, r3, 8, 12			/*	&c	xy	irg		DR	U2		dlc	*/
	strh r5, [r4]				/*	&c	xy	irg		DR	U2		dlc	*/

	/* Output U1 */
	ubfx r6, r3, 20, 12			/*	&c	xy	irg	i12	DR		U1	dlc	*/
	orr r6, 0x1000				/*	&c	xy	irg	i12	DR		U1+	dlc	*/

	/* Output intensity */
	ubfx r5, r2, 20, 12			/*	&c	xy	irg	i12	DR	ihigh	U1+	dlc	*/
	strh r6, [r4]				/*	&c	xy	irg	i12	DR	ihigh		dlc	*/
	ubfx r6, r3, 4, 4			/*	&c	xy	irg	i12	DR	ihigh	ilow	dlc	*/
	bfi r5, r6, 0, 4			/*	&c	xy	irg	i12	DR	i		dlc	*/
	orr r5, 0x5000				/*	&c	xy	irg	i12	DR	i+		dlc	*/

	/* Now - finally - extract consume pointer... */
	uxtb r6, r7, ror 24			/*	&c	xy	irg		DR	i+	cons	dlc	*/

	strh r5, [r4]				/*	&c	xy	irg	i12	DR		cons	dlc	*/

	/* ... consume blue */
	ldmdb r0, { r5, ip }			/*	&c	xy	irg		DR	bscale	cons	dlc	boff	*/
	ldrh r3, [r0, r6, lsl 1]		/*	&c	xy	irg	blue	DR	bscale	cons	dlc	boff	*/
	mla r3, r5, r3, ip			/*	&c	xy	irg	blue	DR		cons	dlc	*/
	usat r3, 12, r3, asr COORD_MAX_EXP			/*	&c	xy	irg	blue	DR		cons	dlc	*/
	orr r3, 0x3000				/*	&c	xy	irg	blue	DR		cons	dlc	*/
	strh r3, [r4]				/*	&c	xy	irg		DR		cons	dlc	*/

	/* Produce red */
	add r0, 44				/*	&c	xy	irg		DR		cons	dlc	*/
	uxtb r5, r7, ror 16			/*	&c	xy	irg		DR	rprod	cons	dlc	*/
	ubfx r3, r2, 12, 12			/*	&c	xy	irg	red	DR	rprod	cons	dlc	*/
	strh r3, [r0, r5, lsl 1]		/*	&c	xy	irg		DR		cons	dlc	*/

	/* Consume red */
	ldmdb r0, { r5, ip }			/*	&c	xy	irg		DR	bscale	cons	dlc	boff	*/
	ldrh r3, [r0, r6, lsl 1]		/*	&c	xy	irg	red	DR	bscale	cons	dlc	boff	*/
	mla r3, r5, r3, ip			/*	&c	xy	irg	red	DR		cons	dlc	*/
	usat r3, 12, r3, asr COORD_MAX_EXP			/*	&c	xy	irg	red	DR		cons	dlc	*/
	orr r3, 0x4000				/*	&c	xy	irg	red	DR		cons	dlc	*/
	strh r3, [r4]				/*	&c	xy	irg		DR			dlc	*/

	/* Produce green - same dance */
	add r0, 40				/*	&c	xy	irg		DR		cons	dlc	*/
	uxtb r5, r7, ror 8			/*	&c	xy	irg		DR	gprod	cons		*/
	ubfx r3, r2, 0, 12			/*	&c	xy		green	DR	gprod	cons		*/
	strh r3, [r0, r5, lsl 1]		/*	&c	xy			DR		cons		*/

	/* Consume green */
	ldmdb r0, { r5, ip }			/*	&c	xy	irg		DR	gscale	cons		goff	*/
	ldrh r3, [r0, r6, lsl 1]		/*	&c	xy		green	DR	gscale	cons		goff	*/
	mla r3, r5, r3, ip			/*		xy		green	DR		cons		*/
	usat r3, 12, r3, asr COORD_MAX_EXP			/*		xy		green	DR		cons		*/
	orr r3, 0x2000				/*		xy		green	DR		cons		*/
	strh r3, [r4]				/*		xy			DR				*/

	/* Get ready to load the transform */
	movw ip, :lower16:transform_matrix
	movt ip, :upper16:transform_matrix

	/* Separate X and Y */
	sxth r5, r1				/*		xy			DR	x			*/
	sxth r6, r1, ror 16			/*					DR	x	y		*/
	mul r7, r5, r6				/*					DR	x	y	xy	*/
	asrs r7, r7, COORD_MAX_EXP		/*					DR	x	y	xys	*/
	
	/* Do the transform */
	ldmia ip!, { r0, r1, r2, r3 }		/*	xc0	yc0	xc1	yc1	DR	x	y	xys	*/
	mul r0, r0, r5				/*	xacc	yc0	xc1	yc1	DR	x	y	xys	*/
	mul r1, r1, r5				/*	xacc	yacc	xc1	yc1	DR		y	xys	*/
	mla r0, r2, r6, r0			/*	xacc	yacc		yc1	DR		y	xys	*/
	mla r1, r3, r6, r1			/*	xacc	yacc			DR				*/
	ldmia ip, { r2, r3, r5, r6 }		/*	xacc	yacc	xc2	yc2	DR	xc3	yc3	xys	*/
	mla r0, r2, r7, r0			/*	xacc	yacc		yc2	DR	xc3	yc3		*/
	add r0, r5, r0, asr COORD_MAX_EXP			/*	xacc	yacc			DR		yc3		*/

	/* Mask and saturate down to 12bit */
	usat r0, 12, r0, asr 4
	orr r0, 0x7000
	strh r0, [r4]
	mla r1, r3, r7, r1			/*	xacc	yacc			DR	xc3	yc3		*/
	add r1, r6, r1, asr COORD_MAX_EXP			/*	xacc	yacc			DR				*/
	usat r1, 12, r1, asr 4
	orr r1, 0x6000
	strh r1, [r4]

	/* Pop our registers */
	pop { r0, r4, r5, r6, r7 }		/*	otime				*/


#if DAC_INSTRUMENT_TIME
	/* Load new SysTick */
	movw r3, 0xE018				/*	otime			&time	*/
	movt r3, 0xE000				/*	otime			&time	*/
	movw r2, :lower16:dac_cycle_count
	movt r2, :upper16:dac_cycle_count
	ldr r1, [r3]				/*	otime	ntime			*/
	sub r0, r1				/*	delta				*/
	str r0, [r2]
#endif

	bx lr

.L.goto_dac_pop_rate_change:
	push { r0, lr }
	movw r0, :lower16:dac_pop_rate_change
	movt r0, :upper16:dac_pop_rate_change
	blx r0
	pop { r0, lr }
	b .L.back_from_pop_rate_change

.L.goto_dac_handle_abstract:
	movw r0, :lower16:dac_handle_abstract
	movt r0, :upper16:dac_handle_abstract
	blx r0

.L.goto_dac_stop_underflow:
	movw r0, :lower16:dac_stop_underflow
	movt r0, :upper16:dac_stop_underflow
	bx r0

PWM1_IRQHandler_end:
