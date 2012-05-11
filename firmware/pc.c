#include <stdio.h>
#include <string.h>
#include <stdarg.h> 
#include <sys/time.h>
#include <dac.h>
#include <skub.h>
#include <stdlib.h>
#include <sdcard.h>
#include <sys/types.h>
#include <unistd.h>
#include <lwip/init.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>
#include <ipv4/lwip/autoip.h>
#include <string.h>
#include <assert.h>
#include <broadcast.h>
#include <tables.h>
#include <lightengine.h>
#include <playback.h>
#include <dac_settings.h>
#include <file_player.h>

dac_settings_t settings;

FILE *outputf_file;

void outputf(const char *fmt, ...) {
	if (!outputf_file) {
		otputf_file = fopen("etherdream-pc.log", "w");
	}
	char buf[160];
	va_list va;
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	printf("%s\n", buf);
	fprintf(outputf_file, "%s\n", buf);
	fflush(outputf_file);
}

int playback_source_flags;
enum playback_source playback_src;
int dac_flags, dac_count, dac_current_pps;

int playback_source_flags;

extern void tp_trianglewave_run(void);

struct periodic_event {
	void (*f)(void);
	int period;
	const char *msg;
	int start;
} const events[] = {
	{ tcp_tmr, 250, "tcp", 100 },
	{ dhcp_coarse_tmr, 60000, "dhcp c", 35 },
	{ dhcp_fine_tmr, 500, "dhcp f", 25 },
	{ autoip_tmr, AUTOIP_TMR_INTERVAL, "autoip", 10 },
	{ broadcast_send, 1000, "broadcast", 10 }
};

int events_last[sizeof(events) / sizeof(events[0])];

TABLE(initializer_t, protocol);
TABLE(initializer_t, hardware);
TABLE(initializer_t, poll);

uint64_t startup_time = 0;

int get_time() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t t = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
	if (startup_time == 0) {
		startup_time = t;
		return 0;
	} else {
		return (t - startup_time);
	}
}

void bail(void) {
	printf("=== Terminating ===\n");
	fflush(stdout);
	fflush(stderr);
	_Exit(0);
}

const thunk_t bail_thunk_begin __attribute__((section(".fini_array.0"))) = bail;
const thunk_t bail_thunk_end __attribute__((section(".fini_array.~"))) = bail;

int main() {
	outputf("=== j4cDAC ===");

	outputf("skub_init()");
	skub_init();

	outputf("lwip_init()");
	lwip_init();

	outputf("Calling initializers...");
	int i;

	for (i = 0; i < TABLE_LENGTH(hardware); i++) {
		printf("initializing %s\n", hardware_table[i].name);
		hardware_table[i].f();
	}

	playback_src = SRC_ILDAPLAYER;

	/* This might have taken some time... */
	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		events_last[i] = events[i].start;
	}

	while (1) {
		/* Check the stuff we check on each loop iteration. */
		for (i = 0; i < TABLE_LENGTH(poll); i++) {
			poll_table[i].f();
		}

		int time = get_time();

		/* Check for periodic events */
		for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
			if (time > events_last[i] + events[i].period) {
				events[i].f();
				events_last[i] += events[i].period;
			}
		}
	}
}

/* DAC Stubs */
uint32_t color_corr_get_offset(int color_index) {
	return 0;
}
uint32_t color_corr_get_gain(int color_index) {
	return 0;
}
void color_corr_set_offset(int color_index, int32_t offset) {
}
void color_corr_set_gain(int color_index, int32_t gain) {
}
int delay_line_get_delay(int color_index) {
	return 0;
}
void delay_line_set_delay(int color_index, int delay) {
}
void serial_send(const char *buf, int len) {
	printf("== %.*s ==", len, buf);
}
uint32_t dac_get_count(void) { return 0; }
char build[32] = "wharrgarbl";
int hw_board_rev = 0;
uint8_t mac_address[] = { 0, 1, 2, 3, 4, 5 };
