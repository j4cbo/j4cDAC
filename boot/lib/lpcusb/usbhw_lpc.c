/*
    LPCUSB, an USB device driver for LPC microcontrollers   
    Copyright (C) 2006 Bertrik Sikken (bertrik@sikken.nl)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. The name of the author may not be used to endorse or promote products
       derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


/** @file
    USB hardware layer
 */

#include "usbdebug.h"

#include "usbhw_lpc.h"
#include "usbapi.h"
#include <minilib.h>

/** Installed endpoint interrupt handlers */
static TFnEPIntHandler  *_apfnEPIntHandlers[16];
/** Installed frame interrupt handlers */
static TFnFrameHandler  *_pfnFrameHandler = NULL;

/**
    Registers an endpoint event callback
        
    @param [in] bEP             Endpoint number
    @param [in] pfnHandler      Callback function
 */
void USBHwRegisterEPIntHandler(uint8_t bEP, TFnEPIntHandler *pfnHandler)
{
    int idx;
    
    idx = EP2IDX(bEP);

    ASSERT(idx<32);

    /* add handler to list of EP handlers */
    _apfnEPIntHandlers[idx / 2] = pfnHandler;
    
    /* enable EP interrupt */
    LPC_USB->USBEpIntEn |= (1 << idx);
    LPC_USB->USBDevIntEn |= EP_SLOW;
    
    DBG("Registered handler for EP 0x%x\n", bEP);
}

/**
    Registers the frame callback
        
    @param [in] pfnHandler  Callback function
 */
void USBHwRegisterFrameHandler(TFnFrameHandler *pfnHandler)
{
    _pfnFrameHandler = pfnHandler;
    
    // enable device interrupt
    LPC_USB->USBDevIntEn |= FRAME;

    DBG("Registered handler for frame\n");
}

void HandleUsbReset(unsigned char bDevStatus);
/**
    USB interrupt handler
        
    @todo Get all 11 bits of frame number instead of just 8

    Endpoint interrupts are mapped to the slow interrupt
 */
void USB_IRQHandler(void)
{
    uint32_t dwStatus;
    uint32_t dwIntBit;
    uint8_t  bEPStat, bDevStat, bStat;
    int i;
    uint16_t wFrame;

    // handle device interrupts
    dwStatus = LPC_USB->USBDevIntSt;
    
    // frame interrupt
    if (dwStatus & FRAME) {
        // clear int
        LPC_USB->USBDevIntClr = FRAME;
        // call handler
        if (_pfnFrameHandler != NULL) {
            wFrame = USBHwCmdRead(CMD_DEV_READ_CUR_FRAME_NR);
            _pfnFrameHandler(wFrame);
        }
    }
    
    // device status interrupt
    if (dwStatus & DEV_STAT) {
        /*  Clear DEV_STAT interrupt before reading DEV_STAT register.
            This prevents corrupted device status reads, see
            LPC2148 User manual revision 2, 25 july 2006.
        */
        LPC_USB->USBDevIntClr = DEV_STAT;
        bDevStat = USBHwCmdRead(CMD_DEV_STATUS);
        if (bDevStat & (CON_CH | SUS_CH | RST)) {
            // convert device status into something HW independent
            bStat = ((bDevStat & CON) ? DEV_STATUS_CONNECT : 0) |
                    ((bDevStat & SUS) ? DEV_STATUS_SUSPEND : 0) |
                    ((bDevStat & RST) ? DEV_STATUS_RESET : 0);
            // call handler
            HandleUsbReset(bStat);
        }
    }
    
    // endpoint interrupt
    if (dwStatus & EP_SLOW) {
        // clear EP_SLOW
        LPC_USB->USBDevIntClr = EP_SLOW;
        // check all endpoints
        for (i = 0; i < 32; i++) {
            dwIntBit = (1 << i);
            if (LPC_USB->USBEpIntSt & dwIntBit) {
                // clear int (and retrieve status)
                LPC_USB->USBEpIntClr = dwIntBit;
                Wait4DevInt(CDFULL);
                bEPStat = LPC_USB->USBCmdData;
                // convert EP pipe stat into something HW independent
                bStat = ((bEPStat & EPSTAT_FE) ? EP_STATUS_DATA : 0) |
                        ((bEPStat & EPSTAT_ST) ? EP_STATUS_STALLED : 0) |
                        ((bEPStat & EPSTAT_STP) ? EP_STATUS_SETUP : 0) |
                        ((bEPStat & EPSTAT_EPN) ? EP_STATUS_NACKED : 0) |
                        ((bEPStat & EPSTAT_PO) ? EP_STATUS_ERROR : 0);
                // call handler
                if (_apfnEPIntHandlers[i / 2] != NULL) {
                    _apfnEPIntHandlers[i / 2](IDX2EP(i), bStat);
                }
            }
        }
    }
}

