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
#include <broadcast.h>
#include <tables.h>
#include <lightengine.h>
#include <playback.h>
#include <ilda-player.h>

int table_hardware_ready = 1;
int table_protocol_ready = 1;
int table_poll_ready = 0;

void outputf(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	vprintf(fmt, va);
	puts("\n");
}

int playback_source_flags;
enum playback_source playback_src;
int dac_flags, dac_count, dac_current_pps;

void eth_get_mac(uint8_t *mac) {
	memcpy(mac, "\x00\x11\x22\x33\x44\x55", 6);
}

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

void playback_refill() {
	int i;

	if (playback_src == SRC_NETWORK)
		return;

	int dlen = dac_request();
	dac_point_t *ptr = dac_request_addr();

	/* Have we underflowed? */
	if (dlen < 0) {
		if (le_get_state() != LIGHTENGINE_READY)
			return;

		outputf("*U*");
		dac_prepare();
		return;
	}

	/* If we don't have any more room... */
	if (dlen == 0) {
		if (dac_get_state() == DAC_PREPARED)
			dac_start();
		return;
	}

	switch (playback_src) {
	case SRC_ILDAPLAYER:
		if (!(playback_source_flags & ILDA_PLAYER_PLAYING))
			break;

		outputf("%d", dlen);

		i = ilda_read_points(dlen, ptr);

		if (i < 0) {
			outputf("err: %d", i);
			playback_source_flags &= ~ILDA_PLAYER_PLAYING;
		} else if (i == 0) {
			ilda_reset_file();

			if (playback_source_flags & ILDA_PLAYER_REPEAT) {
				outputf("rep");
			} else {
				outputf("done");

				/* If the whole file didn't fit in the
				 * buffer, we may have to start it now. */
				dlen = 0;

				playback_source_flags &= ~ILDA_PLAYER_PLAYING;
			}
		} else {
			dac_advance(i);
		}

		break;
	default:
		panic("bad playback source");
	}

	/* If the buffer is nearly full, start it up */
	if (dlen < 200 && dac_get_state() == DAC_PREPARED)
		dac_start();
}

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

void early_setup(void) {
	outputf("=== j4cDAC ===");

	outputf("skub_init()");
	skub_init();

	outputf("lwip_init()");
	lwip_init();
}

const thunk_t early_setup_thunk INIT_ARRAY("0") = early_setup;

void hardware_header(void) {
	outputf("== hardware ==");
}
const thunk_t hardware_header_thunk INIT_ARRAY("hardware.0") = hardware_header; 

void protocol_header(void) {
	outputf("== protocol ==");
}
const thunk_t protocol_header_thunk INIT_ARRAY("protocol.0") = protocol_header; 

int main() {
	outputf("ilda player");
	ilda_open("ildatest.ild");

	outputf("Entering main loop...");

	playback_src = SRC_ILDAPLAYER;
/*
	playback_source_flags = ILDA_PLAYER_PLAYING | ILDA_PLAYER_REPEAT;
*/
//	__enable_irq();
	int i;

	/* This might have taken some time... */
	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		events_last[i] = events[i].start;
	}

	dac_set_rate(12000);
	ilda_set_fps_limit(30);

	table_poll_ready = 1;

	while (1) {
		/* If we're playing from something other than the network,
		 * refill the point buffer. */
		playback_refill();

		/* Check the stuff we check on each loop iteration. */
		for (i = 0; i < TABLE_LENGTH(poll); i++) {
			printf("calling poll...\n");
			poll_table[i].f();
		}

		int time = get_time();

		/* Check for periodic events */
		for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
			if (time > events_last[i] + events[i].period) {
				printf("%s\n", events[i].msg);
				events[i].f();
				events_last[i] += events[i].period;
			}
		}

		usleep(5000);
	}
}
