/*
** usboot_iap.c
** 
** Made by (dave madden)
** Login   <dhm@mersenne.com>
** 
** Started on  Sat Oct 18 06:31:49 2008 dave madden
** Last update Sun May 12 01:17:25 2002 Speed Blue
*/
#include "usboot_iap.h"
#include <LPC17xx.h>

/*
 * Base addreses & lengths of Flash sectors
 */
const IAPSector iapSector[30] = {
    /* Start Addr, Len */
    { 0x00000000,  4096 },
    { 0x00001000,  4096 },
    { 0x00002000,  4096 },
    { 0x00003000,  4096 },
    { 0x00004000,  4096 },
    { 0x00005000,  4096 },
    { 0x00006000,  4096 },
    { 0x00007000,  4096 },
    { 0x00008000,  4096 },
    { 0x00009000,  4096 },
    { 0x0000A000,  4096 },
    { 0x0000B000,  4096 },
    { 0x0000C000,  4096 },
    { 0x0000D000,  4096 },
    { 0x0000E000,  4096 },
    { 0x0000F000,  4096 },
    { 0x00010000, 32768 },
    { 0x00018000, 32768 },
    { 0x00020000, 32768 },
    { 0x00028000, 32768 },
    { 0x00030000, 32768 },
    { 0x00038000, 32768 },
    { 0x00040000, 32768 },
    { 0x00048000, 32768 },
    { 0x00050000, 32768 },
    { 0x00058000, 32768 },
    { 0x00060000, 32768 },
    { 0x00068000, 32768 },
    { 0x00070000, 32768 },
    { 0x00078000, 32768 }
} ;

/*
 * Find the sector containing addr and return its number.  If base and
 * len are nonzero, also return the base address and size.
 */
int
iapFindSector( unsigned long addr, unsigned long *base, unsigned long *size )
{
    unsigned sec;
    
    for (sec = 0; sec < numberof(iapSector); ++sec)
        if (iapSector[sec].base <= addr &&
            iapSector[sec].base + iapSector[sec].size > addr) {
            if (base) *base = iapSector[sec].base;
            if (size) *size  = iapSector[sec].size;
            return (int)sec;
        }

    if (base) *base = 0;
    if (size) *size = 0;
    return -1;
}

/*
 * Erase the sectors containing bytes between dst and dst + len.
 * (This could be multiple sectors.)  Return the IAP result code.
 */
unsigned int
iapErase( unsigned long dst, unsigned len, unsigned int khz )
{
    int              sector;
    unsigned long    old_if;
    unsigned int     cmd[5];
    unsigned int     result[2];

    while (len > 0) {
        /*
         * Look for the sector that contains dst
         */
        for (sector = 0; sector < (int)numberof(iapSector); ++sector)
            if (iapSector[sector].base <= dst &&
                iapSector[sector].base + iapSector[sector].size > dst) break;
        
        if (sector >= (int)numberof(iapSector))
            return IAP_DST_ADDR_NOT_MAPPED; /* not found */

        /*
         * Adjust dst and len if they don't coincide with the sector
         * boundaries.  If (dst + len) extends past the current sector
         * (where dst lies) then just increase length by however much
         * dst is past the beginning of the sector.  Otherwise, we're
         * just trying to erase the sector containing dst.
         */
        if (dst + len > iapSector[sector].base + iapSector[sector].size) {
            len += (dst - iapSector[sector].base);
            dst = iapSector[sector].base;
        } else {
            len = iapSector[sector].size;
            dst = iapSector[sector].base;
        }

        /*
         * Prepare the sector
         */
        cmd[0] = IAP_PREPARE_SECTOR;
        cmd[1] = cmd[2] = sector;
        IAP( cmd, result );

        if (result[0] != IAP_CMD_SUCCESS)
            return result[0];

        /*
         * Erase the sector
         */
        cmd[0] = IAP_ERASE_SECTOR;
        cmd[1] = cmd[2] = sector;
        cmd[3] = khz;
        old_if = __get_PRIMASK();
	__disable_irq( );
        IAP( cmd, result );
        __set_PRIMASK( old_if );
        
        if (result[0] != IAP_CMD_SUCCESS)
            return result[0];
        
        len -= iapSector[sector].size;  // Always >= 0!
        dst += iapSector[sector].size;
    }

    return IAP_CMD_SUCCESS;
}

/*
 * Write len bytes at src to Flash at dst.  Because of IAP
 * limitations, dst must be 256-byte-aligned, src must be
 * word-aligned, and len must be a multiple of 256, 512, 1024
 * or 4096.
 * May perform multiple writes.
 * Returns the IAP result code.
 */
unsigned int
iapWrite( unsigned long dst, const void *src, unsigned len, unsigned int khz )
{
    int              sector;
    unsigned long    old_if;
    unsigned int     cmd[5];
    unsigned int     result[2];

    while (len > 0) {
        /*
         * Look for the sector that contains dst
         */
        sector = iapFindSector( dst, 0, 0 );

        if (sector < 0)
            return IAP_DST_ADDR_NOT_MAPPED; /* not found */

        /*
         * Prepare the sector
         */
        cmd[0] = IAP_PREPARE_SECTOR;
        cmd[1] = cmd[2] = sector;
        IAP( cmd, result );

        if (result[0] != IAP_CMD_SUCCESS)
            return result[0];

        /*
         * Write a block
         */
        cmd[0] = IAP_COPY_RAM_TO_FLASH;
        cmd[1] = dst;
        cmd[2] = (unsigned int)src;
        if (len >= 4096)
            cmd[3] = 4096;
        else
        if (len >= 1024)
            cmd[3] = 1024;
        else
        if (len >= 512)
            cmd[3] = 512;
        else
            cmd[3] = 256;
        cmd[4] = khz;

        old_if = __get_PRIMASK();
	__disable_irq( );
        IAP( cmd, result );
        __set_PRIMASK( old_if );
        
        if (result[0] != IAP_CMD_SUCCESS || len <= cmd[3])
            return result[0];
        
        len -= cmd[3];
        dst += cmd[3];
        src = (const char *)src + cmd[3];
    }

    return IAP_CMD_SUCCESS;
}
