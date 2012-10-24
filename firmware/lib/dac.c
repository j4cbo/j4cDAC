/* j4cDAC DAC driver
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
#include <string.h>
#include <lpc17xx_pwm.h>
#include <lpc17xx_clkpwr.h>
#include <ether.h>
#include <dac.h>
#include <hardware.h>
#include <assert.h>
#include <attrib.h>
#include <transform.h>
#include <lightengine.h>
#include <tables.h>
#include <playback.h>
#include <render.h>

/* Each point is 14 bytes. We buffer 1800 points.
 *
 * This gives us up to 60ms at 30k, 45ms at 40k, 36ms at 50k, or 30ms at
 * 60k.
 */
packed_point_t dac_buffer[DAC_BUFFER_POINTS] AHB0;

#define DAC_MAX_COLOR_DELAY	16

union color_control {
	struct {
		uint8_t blue_produce;
		uint8_t green_produce;
		uint8_t red_produce;
		uint8_t color_consume;
	};
	uint32_t word;
};

volatile struct {
	uint32_t count;
	uint16_t produce;
	uint16_t consume;

	enum {
		IRQ_DO_BUFFER = 14,
		IRQ_DO_ABSTRACT,
		IRQ_DO_PANIC
	} irq_do;
	enum dac_state state;
	enum playback_source playback_src;
	uint8_t pad;

	/* Color channels */
	int32_t blue_gain;
	int32_t blue_offset;
	uint16_t blue_buffer[DAC_MAX_COLOR_DELAY];
	union color_control color_control;
	int32_t red_gain;
	int32_t red_offset;
	uint16_t red_buffer[DAC_MAX_COLOR_DELAY];
	int32_t green_gain;
	int32_t green_offset;
	uint16_t green_buffer[DAC_MAX_COLOR_DELAY];
	uint32_t transform_matrix[8];
} dac_control;

/* Buffer for point rate changes.
 */
static uint32_t dac_rate_buffer[DAC_RATE_BUFFER_SIZE];
static int dac_rate_produce;
static volatile int dac_rate_consume;

uint32_t dac_cycle_count;

/* Internal state. */
int dac_current_pps;
int dac_flags = 0;

/* Shutter pin config. */
#define DAC_SHUTTER_PIN		6
#define DAC_SHUTTER_EN_PIN	7

/* dac_set_rate()
 *
 * Set the DAC point rate to a new value.
 */
int dac_set_rate(int points_per_second) {
	ASSERT(points_per_second <= 200000);
	ASSERT(points_per_second > 0);

	/* The PWM peripheral is set in dac_init() to use CCLK/4. */
	int ticks_per_point = (SystemCoreClock / 4) / points_per_second;
	LPC_PWM1->MR0 = ticks_per_point;

	/* The LDAC low pulse must be at least 20ns long. At CCLK/4 = 24
	 * MHz, one cycle is 42ns. */
	LPC_PWM1->MR5 = ticks_per_point - 1;

	/* Enable this rate to take effect when the timer next overflows. */
	LPC_PWM1->LER = (1<<0) | (1<<5);

	dac_current_pps = points_per_second;

	return 0;
}

/* dac_start()
 *
 * Unsuspend the timers controlling the DAC, and start it playing at a
 * specified point rate.
 */
int dac_start(void) {
	if (dac_control.state != DAC_PREPARED) {
		outputf("dac: not starting - not prepared");
		return -1;
	}

	if (!dac_current_pps) {
		outputf("dac: not starting - no pps");
		return -1;
	}

	outputf("dac: starting");

	dac_control.state = DAC_PLAYING;
	dac_control.playback_src = playback_src;
	dac_control.irq_do = (playback_src == SRC_ABSTRACT ? IRQ_DO_ABSTRACT : IRQ_DO_BUFFER);
	LPC_PWM1->TCR = PWM_TCR_COUNTER_ENABLE | PWM_TCR_PWM_ENABLE;

	led_set_backled(1);
	shutter_set(1);

	return 0;
}

/* dac_rate_queue
 *
 * Queue up a point rate change.
 */
int dac_rate_queue(int points_per_second) {
	if (dac_control.state == DAC_IDLE) {
		outputf("drq rejected: idle");
		return -1;
	}

	int produce = dac_rate_produce;

	int fullness = produce - dac_rate_consume;
	if (fullness < 0)
		fullness += DAC_RATE_BUFFER_SIZE;

	/* The buffer can only ever become one word short of full -
	 * produce = consume means empty.
	 */
	if (fullness >= DAC_RATE_BUFFER_SIZE - 1) {
		outputf("drq rejected: full");
		return -1;
	}

	dac_rate_buffer[produce] = points_per_second;

	dac_rate_produce = (produce + 1) % DAC_RATE_BUFFER_SIZE;

	return 0;
}

/* dac_request
 *
 * "Dear ring buffer: where should I put data and how much should I write?"
 *
 * This returns a number of words (not bytes) to be written to the location
 * give by dac_request_addr().
 */
int dac_request(void) {
	int consume = dac_control.consume;
	int ret;

	if (dac_control.state == DAC_IDLE)
		return -1;

/*
	outputf("d_r: p %d, c %d", dac_control.produce, consume);
*/

	if (dac_control.produce >= consume) {
		/* The read pointer is behind the write pointer, so we can
		 * go ahead and fill the buffer up to the end. */
		if (consume == 0) {
			/* But not if consume = 0, since the buffer can only
			 * ever become one word short of full. */
			ret = (DAC_BUFFER_POINTS - dac_control.produce) - 1;
		} else {
			ret = DAC_BUFFER_POINTS - dac_control.produce;
		}
	} else {
		/* We can only fil up as far as the write pointer. */
		ret = (consume - dac_control.produce) - 1;
	}

	return ret;
}

packed_point_t *dac_request_addr(void) {
	return &dac_buffer[dac_control.produce];
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
	if (dac_control.state == DAC_PREPARED || dac_control.state == DAC_PLAYING) {
		int new_produce = (dac_control.produce + count) % DAC_BUFFER_POINTS;
		dac_control.produce = new_produce;
	}
}

/* delay_line_get_delay
 *
 * Return the number of points of delay in the given delay line.
 */
int delay_line_get_delay(int color_index) {
	union color_control cc = dac_control.color_control;
	int produce;
	if (color_index == 0) produce = cc.red_produce;
	else if (color_index == 1) produce = cc.green_produce;
	else produce = cc.blue_produce;
	return (produce - cc.color_consume + DAC_MAX_COLOR_DELAY) % DAC_MAX_COLOR_DELAY;
}

/* delay_line_reset
 *
 * Reset the delay lines' buffers.
 */
static void delay_line_reset(void) {
	memset(&dac_control.red_buffer, 0, sizeof(&dac_control.red_buffer));
	memset(&dac_control.green_buffer, 0, sizeof(&dac_control.green_buffer));
	memset(&dac_control.blue_buffer, 0, sizeof(&dac_control.blue_buffer));
}

static void move_data_forward(volatile uint16_t *buf, int n) {
	uint16_t buf2[DAC_MAX_COLOR_DELAY];
	int i;
	for (i = 0; i < DAC_MAX_COLOR_DELAY; i++) buf2[i] = buf[i];
	for (i = 0; i < DAC_MAX_COLOR_DELAY; i++)
		buf[i] = buf2[(i + n) % DAC_MAX_COLOR_DELAY];
}

/* delay_line_set_delay
 *
 * Change the delay of a delay line.
 *
 * Reducing a channel's delay will cause some points to be dropped.
 * Increasing it will cause intermediate points to be repeated once.
 */
void delay_line_set_delay(int color_index, int delay) {
	uint32_t produce;
	int i;

	if (delay < 0) delay = 0;
	if (delay >= DAC_MAX_COLOR_DELAY) delay = DAC_MAX_COLOR_DELAY - 1;

	int cur_delay = delay_line_get_delay(color_index);
	int offset = delay - cur_delay;
	outputf("cur delay %d; offset %d", cur_delay, offset);

	if (offset >= 0) {
		/* Repeat some color points. This only requires changing
		 * our one channel's produce pointer and buffer, not the
		 * global consume pointer. */

		if (color_index == 0) {
			__disable_irq();
			produce = dac_control.color_control.red_produce;
			volatile uint16_t *data = dac_control.red_buffer;
			uint16_t repvalue = data[produce];
			for (i = 0; i < offset; i++) {
				produce = (produce + 1) % DAC_MAX_COLOR_DELAY;
				data[produce] = repvalue;
			}
			dac_control.color_control.red_produce = produce;
			__enable_irq();
		} else if (color_index == 1) {
			__disable_irq();
			produce = dac_control.color_control.green_produce;
			volatile uint16_t *data = dac_control.green_buffer;
			uint16_t repvalue = data[produce];
			for (i = 0; i < offset; i++) {
				produce = (produce + 1) % DAC_MAX_COLOR_DELAY;
				data[produce] = repvalue;
			}
			dac_control.color_control.green_produce = produce;
			__enable_irq();
		} else {
			__disable_irq();
			produce = dac_control.color_control.blue_produce;
			volatile uint16_t *data = dac_control.blue_buffer;
			uint16_t repvalue = data[produce];
			for (i = 0; i < offset; i++) {
				produce = (produce + 1) % DAC_MAX_COLOR_DELAY;
				data[produce] = repvalue;
			}
			dac_control.color_control.blue_produce = produce;
			__enable_irq();
		}

	} else {
		/* Move the consume pointer up a bit - but this means that we
		 * also have to update the other two channels. */
		int count = -offset;

		if (color_index == 0) {
			__disable_irq();
			move_data_forward(dac_control.green_buffer, count);
			move_data_forward(dac_control.blue_buffer, count);
			dac_control.color_control.word =
				(dac_control.color_control.word + 0x01000101)
				& 0x0f0f0f0f;
			__enable_irq();
		} else if (color_index == 1) {
			__disable_irq();
			move_data_forward(dac_control.red_buffer, count);
			move_data_forward(dac_control.blue_buffer, count);
			dac_control.color_control.word =
				(dac_control.color_control.word + 0x01010001)
				& 0x0f0f0f0f;
			__enable_irq();
		} else {
			__disable_irq();
			move_data_forward(dac_control.red_buffer, count);
			move_data_forward(dac_control.green_buffer, count);
			dac_control.color_control.word =
				(dac_control.color_control.word + 0x01010100)
				& 0x0f0f0f0f;
			__enable_irq();
		}
	}
}

/* color_corr_get_offset, color_corr_get_gain,
 * color_corr_set_offset, color_corr_set_gain
 *
 * Get/set the gain/offset of a color channel.
 */
uint32_t color_corr_get_offset(int color_index) {
	if (color_index == 0) return dac_control.red_offset;
	else if (color_index == 1) return dac_control.green_offset;
	else return dac_control.blue_offset;
}
uint32_t color_corr_get_gain(int color_index) {
	if (color_index == 0) return dac_control.red_gain;
	else if (color_index == 1) return dac_control.green_gain;
	else return dac_control.blue_gain;
}
void color_corr_set_offset(int color_index, int32_t offset) {
	if (color_index == 0) dac_control.red_offset = offset;
	else if (color_index == 1) dac_control.green_offset = offset;
	else dac_control.blue_offset = offset;
}
void color_corr_set_gain(int color_index, int32_t gain) {
	if (color_index == 0) dac_control.red_gain = gain;
	else if (color_index == 1) dac_control.green_gain = gain;
	else dac_control.blue_gain = gain;
}

extern uint8_t goto_dac16_handle_irq[];
extern uint8_t goto_dac16_handle_irq_end[];
extern char hw_dac_16bit;
extern uint8_t PWM1_IRQHandler[];

/* dac_init
 *
 * Initialize the DAC. This must be called once after reset.
 */
void COLD dac_init() {

	/* Turn on the PWM and timer peripherals. */
	CLKPWR_ConfigPPWR(CLKPWR_PCONP_PCPWM1, ENABLE);
	CLKPWR_SetPCLKDiv(CLKPWR_PCLKSEL_PWM1, CLKPWR_PCLKSEL_CCLK_DIV_4);

	/* Set up the SSP to communicate with the DAC, and initialize to 0 */
	hw_dac_init();

	/* ... and LDAC on the PWM peripheral */
	LPC_PINCON->PINSEL4 |= (1 << 8);

	/* Get the pin set up to produce a low LDAC puslse */
	LPC_GPIO2->FIODIR |= (1 << 4);
	LPC_GPIO2->FIOCLR = (1 << 4);

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

	dac_control.state = DAC_IDLE;
	dac_control.irq_do = IRQ_DO_PANIC;
	dac_control.count = 0;
	dac_current_pps = 0;

	dac_control.color_control.word = 0;
	delay_line_reset();

	dac_control.red_gain = COORD_MAX;
	dac_control.green_gain = COORD_MAX;
	dac_control.blue_gain = COORD_MAX;

	if (hw_dac_16bit) {
		memcpy(PWM1_IRQHandler, goto_dac16_handle_irq, goto_dac16_handle_irq_end - goto_dac16_handle_irq);
	}
}

/* dac_configure
 *
 * Make the DAC ready to accept points. This is required as an explicit
 * reset call after the DAC stops and before the first time it is started.
 */
int dac_prepare(void) {
	if (dac_control.state != DAC_IDLE)
		return -1;

	if (le_get_state() != LIGHTENGINE_READY)
		return -1;

	dac_control.produce = 0;
	dac_control.consume = 0;
	dac_rate_produce = 0;
	dac_rate_consume = 0;
	dac_flags &= ~DAC_FLAG_STOP_ALL;
	dac_control.state = DAC_PREPARED;
	dac_control.irq_do = IRQ_DO_PANIC;

	return 0;
}

/* dac_stop
 *
 * Stop the DAC, set all outputs to zero, and return to the idle state.
 *
 * This is triggered internally when the DAC has a buffer underrun, and may
 * also be called externally at any time.
 */ 
void NOINLINE dac_stop(int flags) {
	/* First things first: shut down the PWM timer. This prevents us
	 * from being interrupted by a PWM interrupt, which could cause
	 * the DAC outputs to be left on. */
	LPC_PWM1->TCR = PWM_TCR_COUNTER_RESET;

	/* Close the shutter. */
	dac_flags &= ~DAC_FLAG_SHUTTER;
	LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_PIN);

	/* Set LDAC to update immediately */
	LPC_PINCON->PINSEL4 &= ~(3 << 8);

	/* Clear out all the DAC channels. */
	hw_dac_zero_all_channels();
	dac_flags &= ~DAC_FLAG_SHUTTER;

	/* Give LDAC back to the PWM hardware */
	LPC_PINCON->PINSEL4 |= (1 << 8);

	/* Now, reset state */
	dac_control.state = DAC_IDLE;
	dac_control.irq_do = IRQ_DO_PANIC;
	dac_control.count = 0;
	dac_flags |= flags;

	delay_line_reset();
}

void dac_pop_rate_change(void) {
	int rate_consume = dac_rate_consume;
	if (rate_consume != dac_rate_produce) {
		dac_set_rate(dac_rate_buffer[rate_consume]);
		rate_consume++;
		if (rate_consume >= DAC_RATE_BUFFER_SIZE)
			rate_consume = 0;
		dac_rate_consume = rate_consume;
	}
}

#if 0
static void __attribute__((always_inline)) dac_write_point(dac_point_t *p) {

	#define MASK_XY(v)	((((v) >> 4) + 0x800) & 0xFFF)

	delay_line_write(&blue_delay, (p->b >> 4) | 0x3000);

	uint32_t xi = p->x, yi = p->y;
	int32_t x = translate_x(xi, yi);
	int32_t y = translate_y(xi, yi);
	LPC_SSP1->DR = MASK_XY(x) | 0x7000;
	LPC_SSP1->DR = MASK_XY(y) | 0x6000;

	delay_line_write(&delay_lines.red, (p->r >> 4) | 0x4000);
	delay_line_write(&delay_lines.green, (p->g >> 4) | 0x2000);

	LPC_SSP1->DR = (p->i >> 4) | 0x5000;
	LPC_SSP1->DR = (p->u1 >> 4) | 0x1000;
	LPC_SSP1->DR = (p->u2 >> 4);
}
#endif

static NOINLINE COLD __attribute__((noreturn)) void dac_panic_not_playing(void) {
	panic("dac_control not PLAYING in PWM1 IRQ");
}

/* dac_handle_abstract
 *
 * To reduce register pressure and improve performance during high-speed
 * network/file playback, the abstract generator codepath is separate from
 * the main PWM IRQ handler.
 */
void NOINLINE dac_handle_abstract() {

	LPC_PWM1->IR = PWM_IR_PWMMRn(0);

	if (unlikely(dac_control.irq_do == IRQ_DO_PANIC)) {
		dac_panic_not_playing();
		return;
	}

	/* If we're not actually playing, produce no output. */
	dac_point_t dp;
	if (playback_source_flags & ABSTRACT_PLAYING) {
		get_next_point(&dp);
	} else {
		memset(&dp, 0, sizeof(dp));
	}

	//dac_write_point(&dp);
	dac_control.count++;
}

enum dac_state dac_get_state(void) {
	return dac_control.state;
}

/* dac_fullness
 *
 * Returns the number of points currently in the buffer.
 */
int dac_fullness(void) {
	int fullness = dac_control.produce - dac_control.consume;
	if (fullness < 0)
		fullness += DAC_BUFFER_POINTS;
	return fullness;
}

/* shutter_set
 *
 * Set the state of the shutter - nonzero for open, zero for closed.
 */
void shutter_set(int state) {
	if (state) {
		__disable_irq();
		if (dac_control.state == DAC_PLAYING) {
			LPC_GPIO2->FIOSET = (1 << DAC_SHUTTER_PIN);
			dac_flags |= DAC_FLAG_SHUTTER;
		}
		__enable_irq();
	} else {
		__disable_irq();
		LPC_GPIO2->FIOCLR = (1 << DAC_SHUTTER_PIN);
		dac_flags &= ~DAC_FLAG_SHUTTER;
		__enable_irq();
	}
}

/* impl_dac_pack_point
 *
 * The actual dac_pack_point function is declared 'static inline' in dac.h,
 * so it will always be inlined. This wrapper forces a copy of the code to
 * be emitted standalone too for inspection purposes.
 */
void impl_dac_pack_point(packed_point_t *dest, dac_point_t *src) __attribute__((used));
void impl_dac_pack_point(packed_point_t *dest, dac_point_t *src) {
	dac_pack_point(dest, src);
}

int32_t __attribute__((used)) impl_translate(int32_t xi, int32_t yi) {
	int32_t x = translate_x(xi, yi);
	int32_t y = translate_y(xi, yi);
	return x ^ y;
}

/* dac_get_count
 *
 * Return the number of points played by the DAC.
 */
uint32_t dac_get_count() {
	return dac_control.count;
}

void dac_stop_underflow() {
	LPC_PWM1->IR = PWM_IR_PWMMRn(0);
	dac_stop(DAC_FLAG_STOP_UNDERFLOW);
}

void dac16_handle_irq(void) {
	LPC_PWM1->IR = PWM_IR_PWMMRn(0);

	uint16_t produce = dac_control.produce;
	uint16_t consume = dac_control.consume;
	if (produce == consume) {
		dac_stop_underflow();
		return;
	}

	packed_point_t *point = &dac_buffer[consume];
	consume++;
	if (consume == DAC_BUFFER_POINTS) consume = 0;
	dac_control.consume = consume;

	uint32_t xv = (((uint16_t)point->x) + 0x8000) << 4;
	uint32_t yv = (((uint16_t)point->y) + 0x8000) << 4;
	hw_dac_write32(0x00100000 | xv);
	hw_dac_write32(0x02300000 | yv);
}

INITIALIZER(hardware, dac_init);
