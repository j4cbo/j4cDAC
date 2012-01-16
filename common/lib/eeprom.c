/* j4cDAC - i2c EEPROM driver
 *
 * Copyright 2011 Jacob Potter
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

#include <hardware.h>
#include <tables.h>
#include <serial.h>

#define I2C		LPC_I2C1
#define I2C_FREQ	50000

/* Default MAC address for prototype boards */
uint8_t mac_address[] = { 0x10, 0x1f, 0xe0, 0x12, 0x1d, 0x0c };

static void eeprom_i2c_start(void) {
	I2C->I2CONCLR = I2C_I2CONCLR_SIC;
	I2C->I2CONSET = I2C_I2CONSET_STA;

	// Wait for complete
	while (!(I2C->I2CONSET & I2C_I2CONSET_SI));
	I2C->I2CONCLR = I2C_I2CONCLR_STAC;
}

static void eeprom_i2c_stop(void) {
	if (I2C->I2CONSET & I2C_I2CONSET_STA)
		I2C->I2CONCLR = I2C_I2CONCLR_STAC;
	I2C->I2CONSET = I2C_I2CONSET_STO;
	I2C->I2CONCLR = I2C_I2CONCLR_SIC;
}

static void eeprom_i2c_send(uint8_t data) {
	if (I2C->I2CONSET & I2C_I2CONSET_STA)
		I2C->I2CONCLR = I2C_I2CONCLR_STAC;
	I2C->I2DAT = data;
	I2C->I2CONCLR = I2C_I2CONCLR_SIC;

	while (!(I2C->I2CONSET & I2C_I2CONSET_SI));
}

static uint8_t eeprom_i2c_recv() {
	I2C->I2CONSET = I2C_I2CONSET_AA;
	I2C->I2CONCLR = I2C_I2CONCLR_SIC;

	while (!(I2C->I2CONSET & I2C_I2CONSET_SI));
	return I2C->I2DAT;
}

static uint8_t eeprom_i2c_recvnak() {
	I2C->I2CONCLR = (I2C_I2CONCLR_AAC | I2C_I2CONCLR_SIC);

	while (!(I2C->I2CONSET & I2C_I2CONSET_SI));
	return I2C->I2DAT;
}

void eeprom_init(void) {

	/* If we don't have an EEPROM, just use the default */
	if (hw_board_rev == HW_REV_PROTO)
		return;

	/* Turn on peripheral */
	I2C->I2SCLH = SystemCoreClock / (4 * I2C_FREQ);
	I2C->I2SCLL = SystemCoreClock / (4 * I2C_FREQ);
	I2C->I2CONCLR = I2C_I2CONCLR_AAC | I2C_I2CONCLR_STAC \
	                | I2C_I2CONCLR_I2ENC;
	I2C->I2CONSET = I2C_I2CONSET_I2EN;

	/* Set pins */
	LPC_PINCON->PINSEL0 |= (3 << 0) | (3 << 2);
	LPC_PINCON->PINMODE_OD0 |= 3;

	/* Read MAC */
	eeprom_i2c_start();
	eeprom_i2c_send(0xA0);
	eeprom_i2c_send(0xFA);
	eeprom_i2c_start();
	eeprom_i2c_send(0xA1);

	int i;
	for (i = 0; i < 5; i++) {
		mac_address[i] = eeprom_i2c_recv();
	}
	mac_address[5] = eeprom_i2c_recvnak();

	eeprom_i2c_stop();

	outputf("MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
		mac_address[0], mac_address[1], mac_address[2],
		mac_address[3], mac_address[4], mac_address[5]);
}
	
INITIALIZER(hardware, eeprom_init);
