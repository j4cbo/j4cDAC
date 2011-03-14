/*****************************************************************************/
/* startup_LPC17xx.s: Startup file for LPC17xx device series				 */
/*****************************************************************************/
/* Version: CodeSourcery Sourcery G++ Lite (with CS3)						*/
/*****************************************************************************/

	.equ	Stack_Size, 0x00002000
	.section ".stack", "w"
	.align 3
	.globl __stack_mem
	.globl __stack_size
__stack_mem:
	.space Stack_Size
__stack_end:
	.size __stack_mem, . - __stack_mem
	.set __stack_size, . - __stack_mem

/* Vector Table */
	.section ".interrupt_vector"
	.globl _interrupt_vector
	.type _interrupt_vector, %object

_interrupt_vector:
	.long   __stack_end		/* Top of Stack			*/
	.long   (_reset - 0x10000000)	/* Reset Handler		*/
	.long   NMI_Handler		/* NMI Handler			*/
	.long   HardFault_Handler	/* Hard Fault Handler		*/
	.long   MemManage_Handler	/* MPU Fault Handler		*/
	.long   BusFault_Handler	/* Bus Fault Handler		*/
	.long   UsageFault_Handler	/* Usage Fault Handler		*/
	.long   0			/* Reserved			*/
	.long   0			/* Reserved			*/
	.long   0			/* Reserved			*/
	.long   0			/* Reserved			*/
	.long   SVC_Handler		/* SVCall Handler		*/
	.long   DebugMon_Handler	/* Debug Monitor Handler	*/
	.long   0			/* Reserved			*/
	.long   PendSV_Handler		/* PendSV Handler		*/
	.long   SysTick_Handler		/* SysTick Handler		*/

	/* External Interrupts */
	.long   WDT_IRQHandler		/* 16: Watchdog Timer		*/
	.long   TIMER0_IRQHandler	/* 17: Timer0		 	*/
	.long   TIMER1_IRQHandler	/* 18: Timer1			*/
	.long   TIMER2_IRQHandler	/* 19: Timer2			*/
	.long   TIMER3_IRQHandler	/* 20: Timer3			*/
	.long   UART0_IRQHandler	/* 21: UART0			*/
	.long   UART1_IRQHandler	/* 22: UART1			*/
	.long   UART2_IRQHandler	/* 23: UART2			*/
	.long   UART3_IRQHandler	/* 24: UART3			*/
	.long   PWM1_IRQHandler		/* 25: PWM1			*/
	.long   I2C0_IRQHandler		/* 26: I2C0			*/
	.long   I2C1_IRQHandler		/* 27: I2C1			*/
	.long   I2C2_IRQHandler		/* 28: I2C2			*/
	.long   SPI_IRQHandler		/* 29: SPI			*/
	.long   SSP0_IRQHandler		/* 30: SSP0			*/
	.long   SSP1_IRQHandler		/* 31: SSP1			*/
	.long   PLL0_IRQHandler		/* 32: PLL0 Lock (Main PLL)	*/
	.long   RTC_IRQHandler		/* 33: Real Time Clock		*/
	.long   EINT0_IRQHandler	/* 34: External Interrupt 0	*/
	.long   EINT1_IRQHandler	/* 35: External Interrupt 1	*/
	.long   EINT2_IRQHandler	/* 36: External Interrupt 2	*/
	.long   EINT3_IRQHandler	/* 37: External Interrupt 3	*/
	.long   ADC_IRQHandler		/* 38: A/D Converter		*/
	.long   BOD_IRQHandler		/* 39: Brown-Out Detect		*/
	.long   USB_IRQHandler		/* 40: USB			*/
	.long   CAN_IRQHandler		/* 41: CAN			*/
	.long   DMA_IRQHandler		/* 42: General Purpose DMA	*/
	.long   I2S_IRQHandler		/* 43: I2S			*/
	.long   ENET_IRQHandler		/* 44: Ethernet			*/
	.long   RIT_IRQHandler		/* 45: Repetitive Int. Timer	*/
	.long   MCPWM_IRQHandler	/* 46: Motor Control PWM	*/
	.long   QEI_IRQHandler		/* 47: Quadrature Encoder	*/
	.long   PLL1_IRQHandler		/* 48: PLL1 Lock (USB PLL)	*/

	.size   _interrupt_vector, . - _interrupt_vector

	.thumb

/* Reset Handler */
	.section ".text"
	.thumb_func
	.globl  _reset
	.type   _reset, %function

_reset:
	/* First things first: copy our .text out of Flash into RAM. */ 
	mov r0, #0		/* Source address */
	ldr r1, =0x10000000	/* Destination address */
	ldr r2, =_copy_end	/* Where to stop */
.L1:
	ldmia r0!, {r3}		/* Read a byte, */
	stmia r1!, {r3}		/* write it back, */
	cmp r1, r2		/* check whether we're done, */
	bne .L1			/* if so, keep going. */

	/* Next step: clear out .bss. */
	mov r0, #0		/* Write zeroes */
	ldr r1, =_bss_start	/* Start */
	ldr r2, =_end		/* End */
.L2:
	stmia r1!, {r0}		/* Write some zeroes, */
	cmp r1, r2		/* check whether we're done, */
	bne .L2			/* if so, keep going. */

	LDR	 R0, =SystemInit
	BLX	 R0
	LDR	 R0, =main
	BX	  R0
	.pool
	.size   _reset,.-_reset


/* Exception Handlers */

	.weak   NMI_Handler
	.type   NMI_Handler, %function
NMI_Handler:
	B	   .
	.size   NMI_Handler, . - NMI_Handler


	.type   HardFault_Handler, %function
HardFault_Handler:
	B	   .
	.size   HardFault_Handler, . - HardFault_Handler


	.weak   MemManage_Handler
	.type   MemManage_Handler, %function
MemManage_Handler:
	B	   .
	.size   MemManage_Handler, . - MemManage_Handler


	.type   BusFault_Handler, %function
BusFault_Handler:
	B	   .
	.size   BusFault_Handler, . - BusFault_Handler

	.weak   UsageFault_Handler
	.type   UsageFault_Handler, %function
UsageFault_Handler:
	B	   .
	.size   UsageFault_Handler, . - UsageFault_Handler

	.weak   SVC_Handler
	.type   SVC_Handler, %function
SVC_Handler:
	B	   .
	.size   SVC_Handler, . - SVC_Handler

	.weak   DebugMon_Handler
	.type   DebugMon_Handler, %function
DebugMon_Handler:
	B	   .
	.size   DebugMon_Handler, . - DebugMon_Handler

	.weak   PendSV_Handler
	.type   PendSV_Handler, %function
PendSV_Handler:
	B	   .
	.size   PendSV_Handler, . - PendSV_Handler

	.weak   SysTick_Handler
	.type   SysTick_Handler, %function
SysTick_Handler:
	B	   .
	.size   SysTick_Handler, . - SysTick_Handler


/* IRQ Handlers */

	.globl  Default_Handler
	.type   Default_Handler, %function
Default_Handler:
	B	   .
	.size   Default_Handler, . - Default_Handler

	.macro  IRQ handler
	.weak   \handler
	.set	\handler, Default_Handler
	.endm

	IRQ	 WDT_IRQHandler
	IRQ	 TIMER0_IRQHandler
	IRQ	 TIMER1_IRQHandler
	IRQ	 TIMER2_IRQHandler
	IRQ	 TIMER3_IRQHandler
	IRQ	 UART0_IRQHandler
	IRQ	 UART1_IRQHandler
	IRQ	 UART2_IRQHandler
	IRQ	 UART3_IRQHandler
	IRQ	 PWM1_IRQHandler
	IRQ	 I2C0_IRQHandler
	IRQ	 I2C1_IRQHandler
	IRQ	 I2C2_IRQHandler
	IRQ	 SPI_IRQHandler
	IRQ	 SSP0_IRQHandler
	IRQ	 SSP1_IRQHandler
	IRQ	 PLL0_IRQHandler
	IRQ	 RTC_IRQHandler
	IRQ	 EINT0_IRQHandler
	IRQ	 EINT1_IRQHandler
	IRQ	 EINT2_IRQHandler
	IRQ	 EINT3_IRQHandler
	IRQ	 ADC_IRQHandler
	IRQ	 BOD_IRQHandler
	IRQ	 USB_IRQHandler
	IRQ	 CAN_IRQHandler
	IRQ	 DMA_IRQHandler
	IRQ	 I2S_IRQHandler
	IRQ	 ENET_IRQHandler
	IRQ	 RIT_IRQHandler
	IRQ	 MCPWM_IRQHandler
	IRQ	 QEI_IRQHandler
	IRQ	 PLL1_IRQHandler

	.end
