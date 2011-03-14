/*
** usboot_iap.h
** 
** Made by dave madden
** Login   <dhm@voodoo.mersenne.com>
** 
** Started on  Fri Oct 17 21:16:04 2008 dave madden
** Last update Fri Oct 17 21:16:04 2008 dave madden
*/

#ifndef   	USBOOT_IAP_H_
# define   	USBOOT_IAP_H_

/*
 * IMPORTANT:  You MUST NOT reference Flash during an Erase or
 * CopyToFlash operation.  Disable interrupts or make sure everything
 * is in RAM (interrupt vector table and ISR).
 */

#define IAP     ((void (*)(unsigned int[], unsigned int[]))0x1fff1ff1)

#ifndef numberof
#   define  numberof(x) (sizeof(x)/sizeof(*(x)))
#endif

/*
 * IAP Commands
 */
#define IAP_PREPARE_SECTOR          50
#define IAP_COPY_RAM_TO_FLASH       51
#define IAP_ERASE_SECTOR            52
#define IAP_BLANK_CHECK_SECTOR      53
#define IAP_READ_PART_ID            54
#define IAP_READ_BOOT_CODE_VERSION  55
#define IAP_COMPARE                 56
#define IAP_REINVOKE_ISP            57
#define IAP_READ_SERIAL             58

/*
 * IAP Error Codes
 */
#define IAP_CMD_SUCCESS             0
#define IAP_INVALID_COMMAND         1
#define IAP_SRC_ADDR_ERROR          2
#define IAP_DST_ADDR_ERROR          3
#define IAP_SRC_ADDR_NOT_MAPPED     4
#define IAP_DST_ADDR_NOT_MAPPED     5
#define IAP_COUNT_ERROR             6
#define IAP_INVALID_SECTOR          7
#define IAP_SECTOR_NOT_BLANK        8
#define IAP_SECTOR_NOT_PREPARED     9
#define IAP_COMPARE_ERROR           10
#define IAP_BUSY                    11
#define IAP_PARAM_ERROR             12
#define IAP_ADDR_ERROR              13
#define IAP_ADDR_NOT_MAPPED         14
#define IAP_CMD_LOCKED              15
#define IAP_INVALID_CODE            16
#define IAP_INALID_BAUD_RATE        17
#define IAP_INVALID_STOP_BIT        18
#define IAP_CODE_READ_PROTECTION    19

typedef struct {
    unsigned long    base;
    unsigned long    size;
} IAPSector;

extern int  iapFindSector( unsigned long addr,
                                    unsigned long *base,
                                    unsigned long *size );

extern unsigned int  iapErase( unsigned long dst,
                               unsigned len,
                               unsigned int clk_khz );

extern unsigned int  iapWrite( unsigned long dst,
                               const void *src,
                               unsigned len,
                               unsigned int clk_khz );

#endif
