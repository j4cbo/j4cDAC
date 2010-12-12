/******************************************************************************
 * @file:    mdio.h
 * @purpose: CMSIS Cortex-M3 Core Peripheral Access Layer Header File for 
 *           NXP LPC17xx Device Series 
 * @version: V1.1
 * @date:    14th May 2009
 *---------------------------------------------------------------------------- */

#ifndef __mdio_H__
#define __mdio_H__


typedef unsigned int       U32;
typedef long long          S64;
typedef unsigned long long U64;
typedef unsigned char      BIT;
typedef unsigned int       BOOL;
#ifndef __TRUE
 #define __TRUE        1
#endif
#ifndef __FALSE
 #define __FALSE       0
#endif

#define MDIO    0x00000200
#define MDC     0x00000100



U32 mdio_read(int PhyReg);
void mdio_write(int PhyReg, int Value);


#endif  // __mdio_H__
