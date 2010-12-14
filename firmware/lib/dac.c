#include <serial.h>
#include <lpc17xx_pinsel.h>
#include <lpc17xx_gpdma.h>
#include <lpc17xx_ssp.h>
#include <lpc17xx_pwm.h>
#include <lpc17xx_clkpwr.h>
#include <lpc17xx_timer.h>

#include <dac.h>

#define DAC_TIMER	LPC_TIM1

#define AHB0 __attribute__((section(".ahb_sram_0")))

#define BUFFER_WORDS		16000
#define BUFFER_BYTES		(BUFFER_WORDS * 2)
#define BUFFER_SEGMENTS		40

#if (BUFFER_WORDS % BUFFER_SEGMENTS)
#error BUFFER_SEGMENTS must evenly divide BUFFER_WORDS
#endif

#define WORDS_PER_SEGMENT	 (BUFFER_WORDS / BUFFER_SEGMENTS)

GPDMA_LLI_Type dac_segments[BUFFER_SEGMENTS] AHB0;
uint16_t dac_buffer[256] AHB0;

/* We have a 40ms buffer.
 *
 * Each point may take up to 16 bytes of buffer (eight 2-byte samples),
 * so we need 32000 bytes. This will straddle the two AHB SRAM sections;
 * alignment will be important.
 *
 * The buffer is divided up into BUF_NUM_SEG segments. Each segment is a
 * scatter-gather list descriptor. The last-filled segment has a null
 * 'next' pointer, so when it is reached, an underflow interrupt will be
 * triggered.
 */

int dac_produce;

/* dac_request
 *
 * "Dear ring buffer: where should I put data and how much should I write?"
 *
 * This returns a number of words (not bytes) to be written. If the return
 * value is nonzero, addrp will have been set to the address to write to. 
 */
int dac_request(uint16_t **addrp) {
	/* Figure out where the current consume pointer is. The DMA
	 * controller may have actually read past whatever is currently
	 * in DMACCSrcAdddr, but it's certainly not before that point. */
	int bytes = (LPC_GPDMACH0->DMACCSrcAddr - ((uint32_t)dac_buffer));
	int consume = bytes / 2;

	outputf("d_r: p %d, c %d", dac_produce, consume);

	if (dac_produce >= consume) {
		/* The read pointer is behind the write pointer, so we can
		 * go ahead and fill the buffer up to the end. */
		if (consume == 0) {
			/* But not if consume = 0, since the buffer can only
			 * ever become one word short of full. */
			return BUFFER_WORDS - 1;
		} else {
			return BUFFER_WORDS - dac_produce;
		}
	} else {
		/* We can only fil up as far as the write pointer. */
		return (consume - dac_produce) - 1;
	}
}


/* dac_advance
 *
 * "Dear ring buffer: I have just added this many words."
 *
 * Call this after writing some number of words to the buffer, as
 * specified by dac_request. It's OK if the invoking code writes *less*
 * than dac_request allowed, but it should not write *more*.
 */
void dac_advance(int count) {
	int new_produce = (dac_produce + count) % BUFFER_WORDS;

	int seg = dac_produce / WORDS_PER_SEGMENT;
	int new_produce_seg = new_produce / WORDS_PER_SEGMENT;

	outputf("d_a: +%d, cs %d, ns %d", count, seg, new_produce_seg);

	/* If we're only partially filling the current segment, stop now */
	if (seg == new_produce_seg) {
		return;
	}

	/* The current segment has now been filled, so we need to link it
	 * in after the previous one. */
	int next = seg;
	seg = (seg + BUFFER_SEGMENTS - 1) % BUFFER_SEGMENTS;

	/* Link in all the newly-valid segments. We stop just *before* the
	 * one in which the produce index now resides. */
	do {
		/* Link in this segment */
		dac_segments[seg].NextLLI = (uint32_t)&dac_segments[next];
		seg = next;
		next = (next + 1) % BUFFER_SEGMENTS;
	} while (next != new_produce_seg);

	dac_produce = new_produce;
}

void dac_init() {

	/* Turn on the PWM peripheral */
	CLKPWR_ConfigPPWR (CLKPWR_PCONP_PCPWM1, ENABLE);
	CLKPWR_SetPCLKDiv (CLKPWR_PCLKSEL_PWM1, CLKPWR_PCLKSEL_CCLK_DIV_4);
	CLKPWR_SetPCLKDiv (CLKPWR_PCLKSEL_TIMER1, CLKPWR_PCLKSEL_CCLK_DIV_4);

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

outputf("ldac");

	/* ... and LDAC on the PWM peripheral */
	PinCfg.Funcnum = 1;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 4;
	PINSEL_ConfigPin(&PinCfg);

	/* XXX match pin */
	PinCfg.Portnum = 1;
	PinCfg.Pinnum = 22;
	PinCfg.Funcnum = 3;
	PINSEL_ConfigPin(&PinCfg);

outputf("dma setup");
	/* Set up DMA. The buffer is initially unlinked. */
	int i;
	for (i = 0; i < BUFFER_SEGMENTS; i++) {
		dac_segments[i].SrcAddr = (int32_t)&dac_buffer[i * WORDS_PER_SEGMENT];
		dac_segments[i].DstAddr = (int32_t)&LPC_SSP1->DR;
		dac_segments[i].NextLLI = 0;
/* XXX: temporarily,  link each segment to itself. */
dac_segments[i].NextLLI = (uint32_t)&dac_segments[i];
		dac_segments[i].Control = WORDS_PER_SEGMENT
			| GPDMA_DMACCxControl_SBSize(GPDMA_BSIZE_4)
			| GPDMA_DMACCxControl_DBSize(GPDMA_BSIZE_4)
			| GPDMA_DMACCxControl_SWidth(GPDMA_WIDTH_HALFWORD) 
			| GPDMA_DMACCxControl_DWidth(GPDMA_WIDTH_HALFWORD) 
			| GPDMA_DMACCxControl_SI;
	}
/*
	for (i = 0; i < BUFFER_WORDS; i++) {
		dac_buffer[i] = (1 << (i % 8));
	}
*/

	/* Turn on the SSP peripheral */
	SSP_CFG_Type SSP_ConfigStruct;
	SSP_ConfigStructInit(&SSP_ConfigStruct);
	SSP_ConfigStruct.CPOL = SSP_CR0_CPOL_HI;
	SSP_ConfigStruct.CPHA = 0;
	SSP_ConfigStruct.ClockRate = 2500000;
	SSP_ConfigStruct.Databit = SSP_DATABIT_16;
	SSP_ConfigStruct.Mode = SSP_MASTER_MODE;
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
	SSP_Cmd(LPC_SSP1, ENABLE);

outputf("dmacon");
	dac_configure(3000);
	return;

	/* The buffer is initially empty. */

	LPC_SSP1->DR = 0xC000;
	delay_ms(1);

	i = 0;
	while(1) {
		LPC_SSP1->DR = 0x6000 | (i & 0xFFF);
		LPC_SSP1->DR = 0x7000 | (i & 0xFFF);
		delay_ms(1);
		i += 2;
	}
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
	LPC_PWM1->MCR = PWM_MCR_RESET_ON_MATCH(0);

	/* Enable single-edge PWM on channel 5 */
	LPC_PWM1->PCR = PWM_PCR_PWMENAn(5);

	/* The LDAC low pulse must be at least 20ns long. At CCLK/4 = 24
	 * MHz, one cycle is 42ns. */
	LPC_PWM1->MR5 = (ticks_per_point * 9) / 10; // XXX for debugging

	/* Now set up the timer similarly. */
	DAC_TIMER->TCR = TIM_ENABLE | TIM_RESET;
	DAC_TIMER->MR0 = ticks_per_point - 1;
	DAC_TIMER->MCR = TIM_RESET_ON_MATCH(0);

	/* mat1 */
	DAC_TIMER->EMR = TIM_EM_SET(0, TIM_EM_TOGGLE);

	/* Turn on the DMA controller */
	LPC_GPDMA->DMACConfig = GPDMA_DMACConfig_E;

	/* Something about synchronizers. WTF. */
	LPC_GPDMA->DMACSync   = 1 << 10;

	/* Use timer as input for DMA request line */
	LPC_SC->DMAREQSEL = 0x5;

	/* Enable DMA ch. 0 */
	LPC_GPDMACH0->DMACCSrcAddr = dac_segments[0].SrcAddr;
	LPC_GPDMACH0->DMACCDestAddr = dac_segments[0].DstAddr;
	LPC_GPDMACH0->DMACCLLI = (uint32_t)&dac_segments[0];
	LPC_GPDMACH0->DMACCControl = dac_segments[0].Control;

	/* Configure DMA ch. 0 */
	LPC_GPDMACH0->DMACCConfig = GPDMA_DMACCxConfig_E
		| GPDMA_DMACCxConfig_DestPeripheral(10)
                | GPDMA_DMACCxConfig_TransferType(GPDMA_TRANSFERTYPE_M2P);

	/* Go! */
	LPC_PWM1->TCR = PWM_TCR_COUNTER_ENABLE | PWM_TCR_PWM_ENABLE;
	DAC_TIMER->TCR = TIM_ENABLE;
}
