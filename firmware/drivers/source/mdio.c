#include "mdio.h"
#include <LPC17xx.h>                    /* LPC17xx definitions               */
#include <lpc17xx_pinsel.h>
#include <hardware.h>

#define DP83848C_DEF_ADR    0x0100      /* Default PHY device address        */

static void delay (void) { __NOP(); __NOP(); __NOP(); __NOP(); };

#define MDIO    0
#define MDC     1

#define PROTO_MDIO	(1 << 9)
#define PROTO_MDC	(1 << 8)
#define PROD_MDIO	(1 << 28)
#define PROD_MDC	(1 << 29)

static inline void mdio_set(U32 val) {
	if (hw_board_rev == HW_REV_PROTO) {
		if (val == MDC) LPC_GPIO2->FIOSET = PROTO_MDC;
		else LPC_GPIO2->FIOSET = PROTO_MDIO;
	} else {
		if (val == MDC) LPC_GPIO4->FIOSET = PROD_MDC;
		else LPC_GPIO4->FIOSET = PROD_MDIO;
	}
}

static inline void mdio_clear(U32 val) {
	if (hw_board_rev == HW_REV_PROTO) {
		if (val == MDC) LPC_GPIO2->FIOCLR = PROTO_MDC;
		else LPC_GPIO2->FIOCLR = PROTO_MDIO;
	} else {
		if (val == MDC) LPC_GPIO4->FIOCLR = PROD_MDC;
		else LPC_GPIO4->FIOCLR = PROD_MDIO;
	}
}

static inline void mdio_set_dir(int state) {
	if (hw_board_rev == HW_REV_PROTO) {
		if (state) LPC_GPIO2->FIODIR |= PROTO_MDIO;
		else LPC_GPIO2->FIODIR &= ~PROTO_MDIO;
	} else {
		if (state) LPC_GPIO4->FIODIR |= PROD_MDIO;
		else LPC_GPIO4->FIODIR &= ~PROD_MDIO;
	}
}

/*--------------------------- output_MDIO -----------------------------------*/

/*--------------------------- output_MDIO -----------------------------------*/

static void output_MDIO (U32 val, U32 n) {

   /* Output a value to the MII PHY management interface. */
   for (val <<= (32 - n); n; val <<= 1, n--) {
      if (val & 0x80000000) {
         mdio_set(MDIO);
      }
      else {
         mdio_clear(MDIO);
      }
      delay ();
      mdio_set(MDC);
      delay ();
      mdio_clear(MDC);
      delay ();
   }
}

/*--------------------------- turnaround_MDIO -------------------------------*/

static void turnaround_MDIO (void) {
   /* Turnaround MDO is tristated. */
   mdio_set_dir(0);
   mdio_set(MDC);
   delay ();
   mdio_clear(MDC);
   delay ();
}

/*--------------------------- input_MDIO ------------------------------------*/

static U32 input_MDIO (void) {

   /* Input a value from the MII PHY management interface. */
   U32 i,val = 0;

   for (i = 0; i < 16; i++) {
      val <<= 1;
      mdio_set(MDC);
      delay ();
      mdio_clear(MDC);
      if (hw_board_rev == HW_REV_PROTO) {
         if (LPC_GPIO2->FIOPIN & PROTO_MDIO) {
            val |= 1;
         }
      } else {
         if (LPC_GPIO4->FIOPIN & PROD_MDIO) {
            val |= 1;
         }
      }
   }
   return (val);
}


U32 mdio_read(int PhyReg) {
   U32 val;

   /* Configuring MDC on P2.8 and MDIO on P2.9 */
   mdio_set_dir(1);

   /* 32 consecutive ones on MDO to establish sync */
   output_MDIO (0xFFFFFFFF, 32);

   /* start code (01), read command (10) */
   output_MDIO (0x06, 4);

   /* write PHY address */
   output_MDIO (DP83848C_DEF_ADR >> 8, 5);

   /* write the PHY register to write */
   output_MDIO (PhyReg, 5);

   /* turnaround MDO is tristated */
   turnaround_MDIO ();

   /* read the data value */
   val = input_MDIO ();

   /* turnaround MDIO is tristated */
   turnaround_MDIO ();

   return (val);
}


void mdio_write(int PhyReg, int Value) {

  /* Configuring MDC on P2.8 and MDIO on P2.9 */
  if (hw_board_rev == HW_REV_PROTO) {
    LPC_GPIO2->FIODIR |= PROTO_MDC;
  } else {
    LPC_GPIO4->FIODIR |= PROD_MDC;
  }
  mdio_set_dir(1);

  /* 32 consecutive ones on MDO to establish sync */
  output_MDIO (0xFFFFFFFF, 32);

  /* start code (01), write command (01) */
  output_MDIO (0x05, 4);

  /* write PHY address */
  output_MDIO (DP83848C_DEF_ADR >> 8, 5);

  /* write the PHY register to write */
  output_MDIO (PhyReg, 5);

  /* turnaround MDIO (1,0)*/
  output_MDIO (0x02, 2);
  
  /* write the data value */
  output_MDIO (Value, 16);

  /* turnaround MDO is tristated */
  turnaround_MDIO ();
}
