/* j4cDAC DMA/DAC driver
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

#include <serial.h>
#include <lpc17xx_pinsel.h>
#include <lpc17xx_gpdma.h>
#include <lpc17xx_ssp.h>
#include <lpc17xx_pwm.h>
#include <lpc17xx_clkpwr.h>
#include <lpc17xx_timer.h>
#include <ether.h>
#include <dac.h>
#include <assert.h>
#include <attrib.h>
#include <lightengine.h>

/* Each point is 18 bytes. We buffer 1800 points = 32400 bytes.
 *
 * This gives us up to 60ms at 30k, 45ms at 40k, 36ms at 50k, or 30ms at
 * 60k.
 */
static dac_point_t dac_buffer[DAC_BUFFER_POINTS] AHB0;

/* Internal state. */
static int dac_produce;
static volatile int dac_consume;
int dac_current_pps;
int dac_count;
enum dac_state dac_state = DAC_IDLE;

/* dac_start()
 *
 * Unsuspend the timers controlling the DAC, and start it playing at a
 * specified point rate.
 */
int dac_start(int points_per_second) {
	if (dac_state != DAC_PREPARED)
		return -1;

	/* The PWM peripheral is set in dac_init() to use CCLK/4. */
	int ticks_per_point = (SystemCoreClock / 4) / points_per_second;
	LPC_PWM1->MR0 = ticks_per_point;

	/* The LDAC low pulse must be at least 20ns long. At CCLK/4 = 24
	 * MHz, one cycle is 42ns. */
	LPC_PWM1->MR5 = ticks_per_point - 1;

	outputf("dac: starting");

	LPC_PWM1->TCR = PWM_TCR_COUNTER_ENABLE | PWM_TCR_PWM_ENABLE;

	dac_state = DAC_PLAYING;
	dac_current_pps = points_per_second;

	return 0;
}

/* dac_request
 *
 * "Dear ring buffer: where should I put data and how much should I write?"
 *
 * This returns a number of words (not bytes) to be written. If the return
 * value is nonzero, addrp will have been set to the address to write to. 
 */
int dac_request(dac_point_t ** addrp) {
	int consume = dac_consume;
	int ret;

	if (dac_state == DAC_IDLE)
		return -1;

/*
	outputf("d_r: p %d, c %d", dac_produce, consume);
*/

	*addrp = &dac_buffer[dac_produce];

	if (dac_produce >= consume) {
		/* The read pointer is behind the write pointer, so we can
		 * go ahead and fill the buffer up to the end. */
		if (consume == 0) {
			/* But not if consume = 0, since the buffer can only
			 * ever become one word short of full. */
			ret = (DAC_BUFFER_POINTS - dac_produce) - 1;
		} else {
			ret = DAC_BUFFER_POINTS - dac_produce;
		}
	} else {
		/* We can only fil up as far as the write pointer. */
		ret = (consume - dac_produce) - 1;
	}

	return ret;
}

/* dac_advance
 *
 * "Dear ring buffer: I have just added this many points."
 *
 * Call this after writing some number of points to the buffer, as
 * specified by dac_request. It's OK if the invoking code writes *less*
 * than dac_request allowed, but it should not write *more*.
 */
void dac_advance(int count) {
	ASSERT(dac_state == DAC_PREPARED || dac_state == DAC_PLAYING);

	int new_produce = (dac_produce + count) % DAC_BUFFER_POINTS;

	dac_produce = new_produce;
}

/* dac_init
 *
 * Initialize the DAC. This must be called once after reset.
 */
void dac_init() {

	/* Turn on the PWM and timer peripherals. */
	CLKPWR_ConfigPPWR(CLKPWR_PCONP_PCPWM1, ENABLE);
	CLKPWR_SetPCLKDiv(CLKPWR_PCLKSEL_PWM1, CLKPWR_PCLKSEL_CCLK_DIV_4);

	/* Set up the SPI pins: SCLK, SYNC, DIN */
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 6;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);

	/* ... and LDAC on the PWM peripheral */
	PinCfg.Funcnum = 1;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 4;
	PINSEL_ConfigPin(&PinCfg);

	/* Turn on the SSP peripheral. */
	LPC_SSP1->CR0 = 0xF | (1 << 6);	/* 16-bit, CPOL = 1; no prescale */
	LPC_SSP1->CR1 = (1 << 1); /* Enable */
	LPC_SSP1->CPSR = 4; /* Divide by 4 -> 24 MHz SPI clock */

	/* Set all the DAC channels to zero except for X/Y, which default
	 * to 0x800 (center). */
	int i;
	for (i = 0; i < 6; i++) {
		LPC_SSP1->DR = (i << 12);
	}
	LPC_SSP1->DR = 0x6800;
	LPC_SSP1->DR = 0x7800;

	/* ... and then power it up. */
	LPC_SSP1->DR = 0xC000;

	/* The PWM peripheral is used to generate LDAC pulses. Set it up,
	 * but hold it in reset until go time. */
	LPC_PWM1->TCR = PWM_TCR_COUNTER_RESET | PWM_TCR_COUNTER_ENABLE;

	/* Reset on match channel 0 */
	LPC_PWM1->MCR = PWM_MCR_RESET_ON_MATCH(0) | PWM_MCR_INT_ON_MATCH(0);

	/* Enable single-edge PWM on channel 5 */
	LPC_PWM1->PCR = PWM_PCR_PWMENAn(5);

	/* The match registers themselves will be set by dac_start(). */

	/* Enable the write-to-DAC interrupt with the highest priority. */
	NVIC_SetPriority(PWM1_IRQn, 0);
	NVIC_EnableIRQ(PWM1_IRQn);

	dac_state = DAC_IDLE;
	dac_count = 0;
	dac_current_pps = 0;
}

/* dac_configure
 *
 * Make the DAC ready to accept points. This is required as an explicit
 * reset call after the DAC stops and before the first time it is started.
 */
int dac_prepare(void) {
	if (dac_state != DAC_IDLE)
		return -1;

	if (le_get_state() != LIGHTENGINE_READY)
		return -1;

	dac_produce = 0;
	dac_consume = 0;
	dac_state = DAC_PREPARED;

	return 0;
}

/* dac_stop
 *
 * Stop the DAC, set all outputs to zero, and return to the idle state.
 *
 * This is triggered internally when the DAC has a buffer underrun, and may
 * also be called externally at any time.
 */ 
void dac_stop(void) {
	/* First things first: cause the PWM system to stop immediately. */
	LPC_PWM1->TCR = PWM_TCR_COUNTER_RESET;

	/* Clear out all the DAC channels. */
	int i;
	for (i = 2; i < 8; i++) {
		LPC_SSP1->DR = (i << 12);
	}

	/* Write an immediate LDAC command */
	LPC_SSP1->DR = 0x9002;

	/* Wait for not-full */
	while (!(LPC_SSP1->SR & SSP_SR_TNF));

	/* Now, reset state */
	dac_state = DAC_IDLE;
	dac_count = 0;
	dac_current_pps = 0;
}

void PWM1_IRQHandler(void) {
	/* Tell the interrupt handler we've handled it */
	if (LPC_PWM1->IR & PWM_IR_PWMMRn(0)) {
		LPC_PWM1->IR = PWM_IR_PWMMRn(0);
	} else {
		panic("Unexpected PWM IRQ");
	}

	ASSERT_EQUAL(dac_state, DAC_PLAYING);

	int consume = dac_consume;

	/* Are we out of buffer space? If so, shut the lasers down. */
	if (consume == dac_produce) {
		dac_stop();
		return;
	}

	#define MASK_XY(v)	((((v) >> 4) + 0x800) & 0xFFF)

	LPC_SSP1->DR = MASK_XY(dac_buffer[consume].x) | 0x6000;
	LPC_SSP1->DR = MASK_XY(dac_buffer[consume].y) | 0x7000;
	LPC_SSP1->DR = (dac_buffer[consume].i >> 4) | 0x5000;
	LPC_SSP1->DR = (dac_buffer[consume].r >> 4) | 0x4000;
	LPC_SSP1->DR = (dac_buffer[consume].g >> 4) | 0x3000;
	LPC_SSP1->DR = (dac_buffer[consume].b >> 4) | 0x2000;
	LPC_SSP1->DR = (dac_buffer[consume].u1 >> 4) | 0x1000;
	LPC_SSP1->DR = (dac_buffer[consume].u2 >> 4);

	dac_count++;

	consume++;

	if (consume >= DAC_BUFFER_POINTS)
		consume = 0;

	dac_consume = consume;
}

enum dac_state dac_get_state(void) {
	return dac_state;
}

/* dac_fullness
 *
 * Returns the number of points currently in the buffer.
 */
int dac_fullness(void) {
	int fullness = dac_produce - dac_consume;
	if (fullness < 0)
		fullness += DAC_BUFFER_POINTS;
	return fullness;
}
