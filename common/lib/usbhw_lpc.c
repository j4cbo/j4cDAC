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

#include <stdint.h>
#include <usbhw_lpc.h>
#include <hardware.h>

/**
    Local function to wait for a device interrupt (and clear it)
        
    @param [in] dwIntr      Bitmask of interrupts to wait for   
 */
void Wait4DevInt(uint32_t dwIntr)
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
uint8_t USBHwCmdRead(uint8_t bCmd)
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
    USBHwEPEnable(idx, 1);
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
    if (hw_board_rev == HW_REV_PROTO) {
        if (fConnect)
            LPC_GPIO1->FIOSET = (1 << 25);
        else
            LPC_GPIO1->FIOCLR = (1 << 25);
    }
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

    /* XXX bootloader had this, firmware didn't? */
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
        if (pbBuf && (i < iMaxLen)) {
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
        if (pbBuf && (i < iMaxLen)) {
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
void USBHwInit(void)
{
	if (hw_board_rev == HW_REV_PROTO) {
		// P1[25] -> pull-up resistor
		LPC_GPIO1->FIOCLR = (1 << 25);
		LPC_GPIO1->FIODIR |= (1 << 25);
	} else {
		// P2.9 -> USB_CONNECT - this is MDIO pin on the LPC1758
		LPC_PINCON->PINSEL4 &= ~0x000C0000;
		LPC_PINCON->PINSEL4 |= 0x00040000;
	}

	// P1.30 -> VBUS
	LPC_PINCON->PINSEL3 &= ~0x30000000;
	LPC_PINCON->PINSEL3 |= 0x20000000;

	// P0.29 -> USB_D+
	// P0.30 -> USB_D-
	LPC_PINCON->PINSEL1 &= ~0x3C000000;
	LPC_PINCON->PINSEL1 |= 0x14000000;

	// enable PUSB
	LPC_SC->PCONP |= (1 << 31);

	// Dev clock, AHB clock enable
	LPC_USB->OTGClkCtrl = 0x12;
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
}
