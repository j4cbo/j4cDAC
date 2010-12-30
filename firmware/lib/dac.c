/* j4cDAC DMA/DAC driver
 *
 * Jacob Potter
 * December 2010
 *
 * Copyright (c) 2010 Jacob Potter. All rights reserved.
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

/* Each point is 18 bytes. We buffer 1800 points = 32400 bytes.
 *
 * This gives us up to 60ms at 30k, 45ms at 40k, 36ms at 50k, or 30ms at
 * 60k.
 */
#define AHB0 __attribute__((section(".ahb_sram_0")))

#define BUFFER_POINTS		1800
#define BUFFER_BYTES		(BUFFER_POINTS * sizeof(dac_point_t))

static dac_point_t dac_buffer[BUFFER_POINTS] AHB0;

/* Internal state. */
static int dac_produce;
static volatile int dac_consume;
enum {
	DAC_NOTREADY,
	DAC_READY,
	DAC_STARTED
} dac_state;

/* dac_go()
 *
 * Unsuspend the timers controlling the DAC.
 */
void dac_go() {
	ASSERT_EQUAL(dac_state, DAC_READY);

	outputf("dac: starting");

	LPC_PWM1->TCR = PWM_TCR_COUNTER_ENABLE | PWM_TCR_PWM_ENABLE;

	dac_state = DAC_STARTED;
}


void DMA_IRQHandler() {
	outputf("WTF: DMA IRQ?");
	while (1);
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

	ASSERT(dac_state == DAC_READY || dac_state == DAC_STARTED);

	if (consume == -1) {
		/* Oops! We underflowed. Shut off the timer. */
		outputf("d_r: underflow!");
		LPC_PWM1->TCR = PWM_TCR_COUNTER_RESET | PWM_TCR_COUNTER_ENABLE;
		dac_state = DAC_NOTREADY;
		return -1;
	}

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
			ret = (BUFFER_POINTS - dac_produce) - 1;
		} else {
			ret = BUFFER_POINTS - dac_produce;
		}
	} else {
		/* We can only fil up as far as the write pointer. */
		ret = (consume - dac_produce) - 1;
	}

	if (ret < 10 && dac_state != DAC_STARTED) {
		dac_go();
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
	ASSERT(dac_state == DAC_READY || dac_state == DAC_STARTED);

	int new_produce = (dac_produce + count) % BUFFER_POINTS;

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

	/* Turn on the SSP peripheral */
	SSP_CFG_Type SSP_ConfigStruct;
	SSP_ConfigStructInit(&SSP_ConfigStruct);
	SSP_ConfigStruct.CPOL = SSP_CR0_CPOL_HI;
	SSP_ConfigStruct.CPHA = 0;
	SSP_ConfigStruct.ClockRate = 25000000;
	SSP_ConfigStruct.Databit = SSP_DATABIT_16;
	SSP_ConfigStruct.Mode = SSP_MASTER_MODE;
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
	SSP_Cmd(LPC_SSP1, ENABLE);

	/* Set all the DAC channels to zero... */
	int i;
	for (i = 0; i < 8; i++) {
		LPC_SSP1->DR = (i << 12);
	}

	/* ... and then power it up. */
	LPC_SSP1->DR = 0xC000;

	/* Enable the write-to-DAC interrupt with the highest priority. */
	NVIC_SetPriority(PWM1_IRQn, 0);
	NVIC_EnableIRQ(PWM1_IRQn);

	dac_state = DAC_NOTREADY;
}

/* dac_configure
 *
 * Configure the DAC for a given point rate.
 */
void dac_configure(int points_per_second) {
	/* The PWM peripheral is set in dac_init() to use CCLK/4. */
	int ticks_per_point = (SystemCoreClock / 4) / points_per_second;

	/* The PWM peripheral is used to generate LDAC pulses. Set it up,
	 * but hold it in reset until go time. */
	LPC_PWM1->TCR = PWM_TCR_COUNTER_RESET | PWM_TCR_COUNTER_ENABLE;

	/* Reset on match channel 0 */
	LPC_PWM1->MR0 = ticks_per_point;
	LPC_PWM1->MCR = PWM_MCR_RESET_ON_MATCH(0) | PWM_MCR_INT_ON_MATCH(0);

	/* Enable single-edge PWM on channel 5 */
	LPC_PWM1->PCR = PWM_PCR_PWMENAn(5);

	/* The LDAC low pulse must be at least 20ns long. At CCLK/4 = 24
	 * MHz, one cycle is 42ns. */
	LPC_PWM1->MR5 = ticks_per_point - 1;

	/* Now everything is ready. The timers will be brought out of reset
	 * in dac_go(). */
	dac_produce = 0;
	dac_consume = 0;
	dac_state = DAC_READY;
}

void dac_shutdown(void) {
	int i;
	for (i = 0; i < 8; i++) {
		LPC_SSP1->DR = (i << 12);
	}
}

void PWM1_IRQHandler(void) {
	/* Tell the interrupt handler we've handled it */
	if (LPC_PWM1->IR & PWM_IR_PWMMRn(0)) {
		LPC_PWM1->IR = PWM_IR_PWMMRn(0);
	} else {
		panic("Unexpected PWM IRQ");
	}

	ASSERT_EQUAL(dac_state, DAC_STARTED);

	int consume = dac_consume;

	/* Are we out of buffer space? If so, shut the lasers down. */
	if (consume == -1 || consume == dac_produce) {
		dac_shutdown();
		dac_consume = -1;
		return;
	}

	LPC_SSP1->DR = ((dac_buffer[consume].x >> 4) & 0xFFF) | 0x6000;
	LPC_SSP1->DR = ((dac_buffer[consume].y >> 4) & 0xFFF) | 0x7000;
	LPC_SSP1->DR = (dac_buffer[consume].i >> 4) | 0x5000;
	LPC_SSP1->DR = (dac_buffer[consume].r >> 4) | 0x4000;
	LPC_SSP1->DR = (dac_buffer[consume].g >> 4) | 0x3000;
	LPC_SSP1->DR = (dac_buffer[consume].b >> 4) | 0x2000;
	LPC_SSP1->DR = (dac_buffer[consume].u1 >> 4) | 0x1000;
	LPC_SSP1->DR = (dac_buffer[consume].u2 >> 4);

	consume++;

	if (consume > BUFFER_POINTS)
		consume = 0;

	dac_consume = consume;
}
