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


#ifdef DEBUG
// comment out the following line if you don't want to use debug LEDs
//#define DEBUG_LED
#endif

#ifdef DEBUG_LED
#ifdef LPC214x
#define DEBUG_LED_ON(x)     IOCLR0 = (1 << x);
#define DEBUG_LED_OFF(x)    IOSET0 = (1 << x);
#define DEBUG_LED_INIT(x)   PINSEL0 &= ~(0x3 << (2*x)); IODIR0 |= (1 << x); DEBUG_LED_OFF(x);
#else
#define DEBUG_LED_ON(x)     FIO2SET = (1 << (x-8));
#define DEBUG_LED_OFF(x)    FIO2CLR = (1 << (x-8));
#define DEBUG_LED_INIT(x)   SCS |= 1; PINSEL10 = 0; FIO2DIR |= (1 << (x-8)); DEBUG_LED_OFF(x);
#endif
#else
#define DEBUG_LED_INIT(x)   /**< LED initialisation macro */
#define DEBUG_LED_ON(x)     /**< turn LED on */
#define DEBUG_LED_OFF(x)    /**< turn LED off */
#endif

/** Installed device interrupt handler */
static TFnDevIntHandler *_pfnDevIntHandler = NULL;
/** Installed endpoint interrupt handlers */
static TFnEPIntHandler  *_apfnEPIntHandlers[16];
/** Installed frame interrupt handlers */
static TFnFrameHandler  *_pfnFrameHandler = NULL;

/** convert from endpoint address to endpoint index */
#define EP2IDX(bEP) ((((bEP)&0xF)<<1)|(((bEP)&0x80)>>7))
/** convert from endpoint index to endpoint address */
#define IDX2EP(idx) ((((idx)<<7)&0x80)|(((idx)>>1)&0xF))



/**
    Local function to wait for a device interrupt (and clear it)
        
    @param [in] dwIntr      Bitmask of interrupts to wait for   
 */
static void Wait4DevInt(uint32_t dwIntr)
{
    // wait for specific interrupt
    while ((LPC_USB->USBDevIntSt & dwIntr) != dwIntr);
    // clear the interrupt bits
    LPC_USB->USBDevIntClr = dwIntr;
}


/**
    Local function to send a command to the USB protocol engine
        
    @param [in] bCmd        Command to send
 */
static void USBHwCmd(uint8_t bCmd)
{
    // clear CDFULL/CCEMTY
    LPC_USB->USBDevIntClr = CDFULL | CCEMTY;
    // write command code
    LPC_USB->USBCmdCode = 0x00000500 | (bCmd << 16);
    Wait4DevInt(CCEMTY);
}


/**
    Local function to send a command + data to the USB protocol engine
        
    @param [in] bCmd        Command to send
    @param [in] bData       Data to send
 */
static void USBHwCmdWrite(uint8_t bCmd, uint16_t bData)
{
    // write command code
    USBHwCmd(bCmd);

    // write command data
    LPC_USB->USBCmdCode = 0x00000100 | (bData << 16);
    Wait4DevInt(CCEMTY);
}


/**
    Local function to send a command to the USB protocol engine and read data
        
    @param [in] bCmd        Command to send

    @return the data
 */
static uint8_t USBHwCmdRead(uint8_t bCmd)
{
    // write command code
    USBHwCmd(bCmd);
    
    // get data
    LPC_USB->USBCmdCode = 0x00000200 | (bCmd << 16);
    Wait4DevInt(CDFULL);
    return LPC_USB->USBCmdData;
}


/**
    'Realizes' an endpoint, meaning that buffer space is reserved for
    it. An endpoint needs to be realised before it can be used.
        
    From experiments, it appears that a USB reset causes LPC_USB->USBReEp to
    re-initialise to 3 (= just the control endpoints).
    However, a USB bus reset does not disturb the USBMaxPSize settings.
        
    @param [in] idx         Endpoint index
    @param [in] wMaxPSize   Maximum packet size for this endpoint
 */
static void USBHwEPRealize(int idx, uint16_t wMaxPSize)
{
    LPC_USB->USBReEp |= (1 << idx);
    LPC_USB->USBEpInd = idx;
    LPC_USB->USBMaxPSize = wMaxPSize;
    Wait4DevInt(EP_RLZED);
}


/**
    Enables or disables an endpoint
        
    @param [in] idx     Endpoint index
    @param [in] fEnable TRUE to enable, FALSE to disable
 */
static void USBHwEPEnable(int idx, int fEnable)
{
    USBHwCmdWrite(CMD_EP_SET_STATUS | idx, fEnable ? 0 : EP_DA);
}


/**
    Configures an endpoint and enables it
        
    @param [in] bEP             Endpoint number
    @param [in] wMaxPacketSize  Maximum packet size for this EP
 */
void USBHwEPConfig(uint8_t bEP, uint16_t wMaxPacketSize)
{
    int idx;
    
    idx = EP2IDX(bEP);
    
    // realise EP
    USBHwEPRealize(idx, wMaxPacketSize);

    // enable EP
    USBHwEPEnable(idx, TRUE);
}


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
    Registers an device status callback
        
    @param [in] pfnHandler  Callback function
 */
void USBHwRegisterDevIntHandler(TFnDevIntHandler *pfnHandler)
{
    _pfnDevIntHandler = pfnHandler;
    
    // enable device interrupt
    LPC_USB->USBDevIntEn |= DEV_STAT;

    DBG("Registered handler for device status\n");
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


/**
    Sets the USB address.
        
    @param [in] bAddr       Device address to set
 */
void USBHwSetAddress(uint8_t bAddr)
{
    USBHwCmdWrite(CMD_DEV_SET_ADDRESS, DEV_EN | bAddr);
}


/**
    Connects or disconnects from the USB bus
        
    @param [in] fConnect    If TRUE, connect, otherwise disconnect
 */
void USBHwConnect(int fConnect)
{
/*
#ifdef LPC23xx
#ifndef LPC2378_PORTB
  if(fConnect)
    FIO2CLR = (1<<9);
  else
    FIO2SET = (1<<9);
#else
  if(fConnect)
    FIO0CLR = (1<<14);
  else
    FIO0SET = (1<<14);
#endif
#endif
*/
if (fConnect)
LPC_GPIO1->FIOSET = (1 << 25);
else
LPC_GPIO1->FIOCLR = (1 << 25);
    USBHwCmdWrite(CMD_DEV_STATUS, fConnect ? CON : 0);

}


/**
    Enables interrupt on NAK condition
        
    For IN endpoints a NAK is generated when the host wants to read data
    from the device, but none is available in the endpoint buffer.
    For OUT endpoints a NAK is generated when the host wants to write data
    to the device, but the endpoint buffer is still full.
    
    The endpoint interrupt handlers can distinguish regular (ACK) interrupts
    from NAK interrupt by checking the bits in their bEPStatus argument.
    
    @param [in] bIntBits    Bitmap indicating which NAK interrupts to enable
 */
void USBHwNakIntEnable(uint8_t bIntBits)
{
    USBHwCmdWrite(CMD_DEV_SET_MODE, bIntBits);
}


/**
    Gets the status from a specific endpoint.
        
    @param [in] bEP     Endpoint number
    @return Endpoint status byte (containing EP_STATUS_xxx bits)
 */
uint8_t  USBHwEPGetStatus(uint8_t bEP)
{
    int idx = EP2IDX(bEP);

    return USBHwCmdRead(CMD_EP_SELECT | idx);
}


/**
    Sets the stalled property of an endpoint
        
    @param [in] bEP     Endpoint number
    @param [in] fStall  TRUE to stall, FALSE to unstall
 */
void USBHwEPStall(uint8_t bEP, int fStall)
{
    int idx = EP2IDX(bEP);

    USBHwCmdWrite(CMD_EP_SET_STATUS | idx, fStall ? EP_ST : 0);
}


/**
    Writes data to an endpoint buffer
        
    @param [in] bEP     Endpoint number
    @param [in] pbBuf   Endpoint data
    @param [in] iLen    Number of bytes to write
            
    @return number of bytes written into the endpoint buffer
*/
int USBHwEPWrite(uint8_t bEP, uint8_t *pbBuf, int iLen)
{
    int idx;
    
    idx = EP2IDX(bEP);
    
    // set write enable for specific endpoint
    LPC_USB->USBCtrl = WR_EN | ((bEP & 0xF) << 2);
    
    // set packet length
    LPC_USB->USBTxPLen = iLen;
    
    // write data
    while (LPC_USB->USBCtrl & WR_EN) {
        LPC_USB->USBTxData = (pbBuf[3] << 24) | (pbBuf[2] << 16) | (pbBuf[1] << 8) | pbBuf[0];
        pbBuf += 4;
    }

    LPC_USB->USBCtrl = 0;

    // select endpoint and validate buffer
    USBHwCmd(CMD_EP_SELECT | idx);
    USBHwCmd(CMD_EP_VALIDATE_BUFFER);
    
    return iLen;
}


/**
    Reads data from an endpoint buffer
        
    @param [in] bEP     Endpoint number
    @param [in] pbBuf   Endpoint data
    @param [in] iMaxLen Maximum number of bytes to read
            
    @return the number of bytes available in the EP (possibly more than iMaxLen),
    or <0 in case of error.
 */
int USBHwEPRead(uint8_t bEP, uint8_t *pbBuf, int iMaxLen)
{
    int i, idx;
    uint32_t dwData, dwLen;
    
    idx = EP2IDX(bEP);
    
    // set read enable bit for specific endpoint
    LPC_USB->USBCtrl = RD_EN | ((bEP & 0xF) << 2);
    
    // wait for PKT_RDY
    do {
        dwLen = LPC_USB->USBRxPLen;
    } while ((dwLen & PKT_RDY) == 0);
    
    // packet valid?
    if ((dwLen & DV) == 0) {
        return -1;
    }
    
    // get length
    dwLen &= PKT_LNGTH_MASK;
    
    // get data
    dwData = 0;
    for (i = 0; i < dwLen; i++) {
        if ((i % 4) == 0) {
            dwData = LPC_USB->USBRxData;
        }
        if ((pbBuf != NULL) && (i < iMaxLen)) {
            pbBuf[i] = dwData & 0xFF;
        }
        dwData >>= 8;
    }

    // make sure RD_EN is clear
    LPC_USB->USBCtrl = 0;

    // select endpoint and clear buffer
    USBHwCmd(CMD_EP_SELECT | idx);
    USBHwCmd(CMD_EP_CLEAR_BUFFER);
    
    return dwLen;
}




int USBHwISOCEPRead(const uint8_t bEP, uint8_t *pbBuf, const int iMaxLen)
{
    int i, idx;
    uint32_t dwData, dwLen;

    idx = EP2IDX(bEP);

    // set read enable bit for specific endpoint
    LPC_USB->USBCtrl = RD_EN | ((bEP & 0xF) << 2);
    
    //Note: for some reason the USB perepherial needs a cycle to set bits in USBRxPLen before 
    //reading, if you remove this ISOC wont work. This may be a but in the chip, or due to 
    //a mis-understanding of how the perepherial is supposed to work.    
    asm volatile("nop\n"); 

    
    dwLen = LPC_USB->USBRxPLen;
    if( (dwLen & PKT_RDY) == 0 ) {
        LPC_USB->USBCtrl = 0;// make sure RD_EN is clear
        return(-1);
    }

    // packet valid?
    if ((dwLen & DV) == 0) {
        LPC_USB->USBCtrl = 0;// make sure RD_EN is clear
        return -1;
    }

    // get length
    dwLen &= PKT_LNGTH_MASK;

    // get data
    dwData = 0;
    for (i = 0; i < dwLen; i++) {
        if ((i % 4) == 0) {
            dwData = LPC_USB->USBRxData;
        }
        if ((pbBuf != NULL) && (i < iMaxLen)) {
            pbBuf[i] = dwData & 0xFF;
        }
        dwData >>= 8;
    }

    // make sure RD_EN is clear
    LPC_USB->USBCtrl = 0;

    // select endpoint and clear buffer
    USBHwCmd(CMD_EP_SELECT | idx);
    USBHwCmd(CMD_EP_CLEAR_BUFFER);

    return dwLen;
}


/**
    Sets the 'configured' state.
        
    All registered endpoints are 'realised' and enabled, and the
    'configured' bit is set in the device status register.
        
    @param [in] fConfigured If TRUE, configure device, else unconfigure
 */
void USBHwConfigDevice(int fConfigured)
{
    // set configured bit
    USBHwCmdWrite(CMD_DEV_CONFIG, fConfigured ? CONF_DEVICE : 0);
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

// LED9 monitors total time in interrupt routine
DEBUG_LED_ON(9);

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


//            if (_pfnDevIntHandler != NULL) {
DEBUG_LED_ON(8);        
HandleUsbReset(bStat);
//                _pfnDevIntHandler(bStat);
DEBUG_LED_OFF(8);       
//            }
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
DEBUG_LED_ON(10);       
                    _apfnEPIntHandlers[i / 2](IDX2EP(i), bStat);
DEBUG_LED_OFF(10);
                }
            }
        }
    }
    
DEBUG_LED_OFF(9);       
}



/**
    Initialises the USB hardware
        
    This function assumes that the hardware is connected as shown in
    section 10.1 of the LPC2148 data sheet:
    * P0.31 controls a switch to connect a 1.5k pull-up to D+ if low.
    * P0.23 is connected to USB VCC.
    
    Embedded artists board: make sure to disconnect P0.23 LED as it
    acts as a pull-up and so prevents detection of USB disconnect.
        
    @return TRUE if the hardware was successfully initialised
 */
int USBHwInit(void)
{
	// P2.9 -> USB_CONNECT - this is MDIO pin on the LPC1758
	#if defined(_LPC_SERIES_176x_)
	LPC_PINCON->PINSEL4 &= ~0x000C0000;
	LPC_PINCON->PINSEL4 |= 0x00040000;
	#endif

	// P1.18 -> USB_UP_LED
	// P1.30 -> VBUS
	LPC_PINCON->PINSEL3 &= ~0x30000030;
	LPC_PINCON->PINSEL3 |= 0x20000010;

	// P0.29 -> USB_D+
	// P0.30 -> USB_D-
	LPC_PINCON->PINSEL1 &= ~0x3C000000;
	LPC_PINCON->PINSEL1 |= 0x14000000;

	// enable PUSB
	LPC_SC->PCONP |= (1 << 31);

	LPC_USB->OTGClkCtrl = 0x12;                       /* Dev clock, AHB clock enable  */
	while ((LPC_USB->OTGClkSt & 0x12) != 0x12);

	//clear all interrupts for now
	LPC_USB->USBDevIntEn = 0;
	LPC_USB->USBDevIntClr = 0xFFFFFFFF;
	LPC_USB->USBDevIntPri = 0;

	LPC_USB->USBEpIntEn = 0;
	LPC_USB->USBEpIntClr = 0xFFFFFFFF;
	LPC_USB->USBEpIntPri = 0;

	// by default, only ACKs generate interrupts
	USBHwNakIntEnable(0);

	return TRUE;
}





/**
    This function is used to setup and populated the various elements of a LPC2148 DMA desccriptor such that
    after calling this function, the DMA descriptor could be used as part of a DMA tranfer.
    
        
    @param [in] dmaDescriptor    A pointer to a 4 or 5 element long array of uint32_t's that the DMA descriptor data is to be stored into, it should point to some place in DMA RAM.
    @param [in] nextDdPtr        The value to be placed in the "Next_DD_Pointer" value of the DMA desriptor, set to NULL if there is no next-pointer.
    @param [in] isIsocFlag       Flag to indicate if this DMA descriptor is for an ISOC endpoint (with a 5 element dmaDescriptor array pointer), 1 indicates ISOC, 0 indicates non-ISOC. 
    @param [in] maxPacketSize    The maximum packet size that can be sent/received for the endpoint in question.
    @param [in] dmaLengthIsocNumFrames    For non-ISOC endpoints, the number of bytes in the buffer to be transfered, for ISOC endpoints, the number of frames to be transfered.
    @param [in] dmaBufferStartAddress    Start address for the dma transfer (location to store data for an OUT endpoint, location to pull data from for an IN endpoint), it should point to some place in DMA RAM.
    @param [in] isocPacketSizeMemoryAddress   If a non-ISOC endpoint, set this to NULL, if an ISOC endpoint, then set this to a pointer to an array of uint32_t's that represent how

    @return  void
 */
void USBSetupDMADescriptor(
		volatile uint32_t dmaDescriptor[], 
		volatile uint32_t nextDdPtr[],
		const uint8_t isIsocFlag, 
		const uint16_t maxPacketSize, 
		const uint16_t dmaLengthIsocNumFrames,
		void *dmaBufferStartAddress,
		uint32_t *isocPacketSizeMemoryAddress ) 
{
	dmaDescriptor[1] = 0;
	dmaDescriptor[0] = (uint32_t) nextDdPtr;
	dmaDescriptor[1] |= ((maxPacketSize & 0x3FF) << 5);//Set maxPacketSize
	dmaDescriptor[1] |= (dmaLengthIsocNumFrames << 16);//aka number of ISOC packets if in ISOC mode
	if( isIsocFlag ) {
		dmaDescriptor[1] |= (1<<4);//enable isoc type
	}
	if( nextDdPtr != NULL ) {
		dmaDescriptor[1] |= (1<<2); //mark next DD as valid
	}
	dmaDescriptor[2] = (uint32_t) dmaBufferStartAddress;
	
	if( isIsocFlag && isocPacketSizeMemoryAddress != NULL ) {
		dmaDescriptor[4] = (uint32_t) isocPacketSizeMemoryAddress;
	}
	dmaDescriptor[3] = 0; //mark DD as valid and reset all status bits
}

/**
    This function disables DMA for the endpoint indicated by the paramater.
        
    @param [in] bEndpointNumber the endpoint number to be disabled.

    @return   void
 */
void USBDisableDMAForEndpoint(const uint8_t bEndpointNumber) {
	int idx = EP2IDX(bEndpointNumber);
	LPC_USB->USBEpDMADis = (1<<idx);
}

/**
    Enables DMA for a particular endpoint.
        
    @param [in] bEndpontNumber The endpoint to to enable DMA for.

    @return 
 */
void USBEnableDMAForEndpoint(const uint8_t bEndpointNumber) {
	int idx = EP2IDX(bEndpointNumber);
	LPC_USB->USBEpDMAEn = (1<<idx);
}

/**
    This function is used to initialized an ISOC frame array
        
    @param [in|out] isocFrameArr        The array of ISOC frame descriptors to be set, it should point to some place in DMA RAM.
    @param [in] numElements             The number of elements in the isocFrameArr array.
    @param [in] startFrameNumber        Number to start at for numbering each frame.
    @param [in] defaultFrameLength      The frame length to set all the frame descriptors to.

    @return  void
 */
void USBInitializeISOCFrameArray(uint32_t isocFrameArr[], const uint32_t numElements, const uint16_t startFrameNumber, const uint16_t defaultFrameLength) {
	uint16_t i;
	uint16_t frameNumber = startFrameNumber;
	
	for(i = 0; i < numElements; i++ ) {
		isocFrameArr[i] = (frameNumber<<16) | (1<<15) | (defaultFrameLength & 0x3FF);
		frameNumber++;
	}
}

/**
    This function is used to set the DMA descriptor head for the linked list of a particular endpoint.
        
    @param [in] bEp                    The endpoint number to set the DMA linked list head for.
    @param [in|out] udcaHeadArray      The array of pointers that point to the head DD elements for each endpoint, it should point to some place in DMA RAM.
    @param [in|out] dmaDescriptorPtr   The address of the DMA descriptor that is to be the new head, it should point to some place in DMA RAM.

    @return 
 */
void USBSetHeadDDForDMA(const uint8_t bEp, volatile uint32_t* udcaHeadArray[32], volatile const uint32_t *dmaDescriptorPtr) {
	udcaHeadArray[EP2IDX(bEp)] = (uint32_t) dmaDescriptorPtr;
}

/**
    This function is used to initialize the USB DMA controller
        
    @param [in|out] udcaHeadArray   This is a pointer to the array of DMA descriptor linked-list head pointers, it should point to some place in DMA RAM.

    @return 
 */
void USBInitializeUSBDMA(volatile uint32_t* udcaHeadArray[32]) {
	//set following 32 pointers to be null
	int i;
	for(i = 0; i < 32; i++ ) {
		udcaHeadArray[i] = NULL;
	}
	LPC_USB->USBUDCAH = (uint32_t) udcaHeadArray;
}










