#ifndef DAC_H
#define DAC_H

typedef struct dac_point {
	uint16_t control;
	int16_t x;
	int16_t y;
	uint16_t r;
	uint16_t g;
	uint16_t b;
	uint16_t i;
	uint16_t u1;
	uint16_t u2;
} dac_point_t;

void dac_init(void);
void dac_configure(int points_per_second);
int dac_request(dac_point_t **addrp);
void dac_advance(int count);

#endif
