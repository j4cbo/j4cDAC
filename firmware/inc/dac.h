#ifndef DAC_H
#define DAC_H

void dac_init(void);
void dac_configure(int points_per_second);
int dac_request(uint16_t **addrp);
void dac_advance(int count);

#endif
