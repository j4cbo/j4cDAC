#define _GNU_SOURCE

#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include "etherdream.h"

#define CIRCLE_POINTS	600

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct etherdream_point circle[CIRCLE_POINTS];

uint16_t colorsin(float pos) {
	int res = (sin(pos) + 1) * 32768;
	if (res < 0) return 0;
	if (res > 65535) return 65535;
	return res;
}

void fill_circle(float phase) {
	int i;
	for (i = 0; i < CIRCLE_POINTS; i++) {
		struct etherdream_point *pt = &circle[i];
		float ip = (float)i * 2.0 * M_PI / (float)CIRCLE_POINTS;
		pt->x = sin(ip) * 20000; 
		pt->y = cos(ip) * 20000; 
		pt->r = colorsin(ip + phase);
		pt->g = colorsin(ip + (2.0 * M_PI / 3.0) + phase);
		pt->b = colorsin(ip + (4.0 * M_PI / 3.0) + phase);
	}
}

int main() {
	etherdream_lib_start();

	/* Sleep for a bit over a second, to ensure that we see broadcasts
	 * from all available DACs. */
	usleep(1200000);

	int cc = etherdream_dac_count();
	if (!cc) {
		printf("No DACs found.\n");
		return 0;
	}

	int i;
	for (i = 0; i < cc; i++) {
		printf("%d: Ether Dream %06lx\n", i,
			etherdream_get_id(etherdream_get(i)));
	}

	struct etherdream *d = etherdream_get(0);

	printf("Connecting...\n");
	if (etherdream_connect(d) < 0)
		return 1;

	i = 0;
	while (1) {
		fill_circle((float)i / 50);
		int res = etherdream_write(d, circle, CIRCLE_POINTS, 30000, 1);
		if (res != 0) {
			printf("write %d\n", res);
		}
		etherdream_wait_for_ready(d);
		i++;
	}

	printf("done\n");
	return 0;
}
