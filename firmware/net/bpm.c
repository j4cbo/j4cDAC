/* j4cDAC tap-for-bpm master oscillator
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

/* We use one of the general-purpose timers to track our 't' value. These
 * timers count up at the CPU clock rate. At each point, t = (count / cmax).
 *
 * The oscillator has four states: off, init, running, and reinit. In 'off',
 * it is not counting at all. The first tap causes it to enter 'init'. The
 * next tap causes it to enter 'running'. After that, a tap may cause it to
 * enter the 'reinit' stage, for changing bpm on the fly. 
 *
 * When the first tap occurs, the counter is startd. The handler for the
 * next tap examines the counter, resets it, and sets count_max to the
 * previous value.
 * 
 * After that, a tap is used to refine the oscillator's estimate of the bpm.
 * If a tap happens at a significantly unexpected time, the oscillator will
 * change to the 'reinit' state, so that it more quickly converges on a new
 * bpm setting.
 *
 * The oscillator's t value must smoothly ramp across the same range on
 * every cycle. This means that there are actually two beats that are tracked.
 * The tap input's frequency is tracked in 'freq' and its phase, relative to
 * the current counter value, is in 'tap_delta'. The counter itself forms a
 * second oscillator that tracks the beat in frequency and phase.
 */

#include <LPC17xx.h>
#include <LPC17xx_bits.h>
#include <assert.h>
#include <serial.h>
#include <osc.h>
#include <stdio.h>

#define BPM_TIMER	LPC_TIM0
#define BPM_IRQHandler	TIMER0_IRQHandler
#define BPM_TIMER_IRQn	TIMER0_IRQn

/* WINDOW is the numerator of the window for a tap to be accepted as part of
 * the current beat, rather than resetting the bpm counter. WINDOW=3 means
 * that the tap must be between 2/3 and 4/3 of the expected time, and so on.
 */
#define WINDOW	4

volatile static int bpm_work;

static int bpm_freq;

volatile static int bpm_last_tap;

enum bpm_state {
	BPM_INIT,
	BPM_FIRST,
	BPM_RUNNING,
	BPM_RESTART
};

static enum bpm_state bpm_state = BPM_INIT;

void bpm_init(void) {
	/* Set up the timer */
	BPM_TIMER->TCR = TnTCR_Counter_Enable |  TnTCR_Counter_Reset;

	/* Prescale: divide by 24 to give us a 4 MHz clock. This lets us
	 * count as low as 2 bpm while still having excessive precision. */
	BPM_TIMER->PR = 23;

	/* Interrupt and reset on match 0. */
	BPM_TIMER->MCR = 3;

	/* Enable the interrupt */
	NVIC_SetPriority(BPM_TIMER_IRQn, 3);
	NVIC_EnableIRQ(BPM_TIMER_IRQn);

}

void BPM_IRQHandler(void) {
	/* Assert that the right thing caused this IRQ */
	if (BPM_TIMER->IR & 1) {
		BPM_TIMER->IR = 1;
	} else {
		panic("Unexpected bpm timer IRQ");
	}

	/* Update last_tap to account for the fact that the timer
	 * overflowed */
	int count_max = BPM_TIMER->MR0;
	int last_tap = bpm_last_tap;

	if (last_tap > -count_max) {
		last_tap -= count_max;
		bpm_last_tap = last_tap;
	}

	int phase;

	if (last_tap < (-3 * bpm_freq / 2)) {
		/* It's been a while. */
		phase = 0;
 	} else if (last_tap < (-bpm_freq / 2)) {
		/* The tick came too early. */
		phase = last_tap + bpm_freq;
	} else {
		phase = last_tap;
	}

	count_max = (count_max / 2) + ((bpm_freq + phase) / 2);
	BPM_TIMER->MR0 = count_max;

	bpm_work = 1;
}

void bpm_tap(void) {
	int last_tap = bpm_last_tap;
	int counter = BPM_TIMER->TC;
	int tap_delta = counter - last_tap;

	/* last_tap may have changed, if an interrupt happened */
	int last_tap_2 = bpm_last_tap;
	if (last_tap != last_tap_2) {
		tap_delta = BPM_TIMER->TC - last_tap_2;
	}

	switch (bpm_state) {
	case BPM_INIT:
		/* First tap */
		bpm_state = BPM_FIRST;
		BPM_TIMER->TCR = TnTCR_Counter_Enable;
		BPM_TIMER->MR0 = -1;
		break;

	case BPM_FIRST:
		/* Second tap. Now we have an initial guess at the
		 * frequency. */
		bpm_freq = tap_delta;
		BPM_TIMER->MR0 = tap_delta;
		BPM_TIMER->TC = 0;
		bpm_state = BPM_RUNNING;
		break;

	case BPM_RUNNING:
		/* If we've missed a tap, tap_delta will be very negative -
		 * we might see that it's been (1.95 * freq) ticks since the
		 * last tap, for instance. Ignore the missing beats and
		 * this tap normally. */
		if (tap_delta > ((WINDOW + 1) * bpm_freq / WINDOW)) {
			tap_delta -= bpm_freq;
		}

		/* Did this tap come in at a "reasonable time"? */
		if (tap_delta > ((WINDOW - 1) * bpm_freq / WINDOW)) {
			bpm_freq = (bpm_freq * 9) / 10 + (tap_delta / 10);
		} else {
			/* It didn't. Resync. */
			bpm_state = BPM_RESTART;
			outputf("RESET");
		}

		break;

	case BPM_RESTART:
		bpm_freq = tap_delta;
		bpm_state = BPM_RUNNING;
		break;
	}

	bpm_last_tap = counter;
}

static int bpm_led_state = 0;

void bpm_check() {
	if (!bpm_work) return;
	bpm_work = 0;

	if (bpm_led_state) {
		bpm_led_state = 0;
	} else {
		bpm_led_state = 1;
	}

	char buf[20];

	sprintf(buf, "%lu bpm", 60000000 / BPM_TIMER->MR0);

	osc_send_int("/1/led1", bpm_led_state);
	osc_send_string("/1/label1", buf);
}
