/* j4cDAC driver
 *
 * Copyright 2009 Andrew Kibler
 * Copyright 2011 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or 3, or the GNU Lesser General Public License version 3, as published
 * by the Free Software Foundation, at your option.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define WINVER 0x0501

#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "dac.h"

extern dac_t * dac_list;

unsigned __stdcall LoopUpdate(void *d);

/* Initialize a dac_t */
int dac_init(dac_t *d) {
	memset(d, 0, sizeof(*d));
	InitializeCriticalSection(&d->buffer_lock);
	return 0;
}

/* Connect */
int dac_open_connection(dac_t *d) {
	char host[40];
	strncpy(host, inet_ntoa(d->addr), sizeof(host) - 1);
	host[sizeof(host) - 1] = 0;

	// Connect to the DAC
	if (dac_connect(&d->conn, host, "7765") < 0) {
		flog("!! DAC connection failed.\n");
		return -1;
	}

	// Fire off the worker thread
	d->loop_go = CreateEvent(NULL, FALSE, FALSE, NULL);
	d->state = ST_READY;

	d->workerthread = (HANDLE)_beginthreadex(NULL, 0, &LoopUpdate, d, 0, NULL);
	if (!d->workerthread) {
		flog("!! Begin thread error: %s\n", strerror(errno));
		return -1;
	}

	flog("Ready.\n");

	return 0;
}

void dac_close_connection(dac_t *d) {
	EnterCriticalSection(&d->buffer_lock);
	if (d->state == ST_READY)
		SetEvent(d->loop_go);
	d->state = ST_SHUTDOWN;
	LeaveCriticalSection(&d->buffer_lock);

	int rv = WaitForSingleObject(d->workerthread, 250);
	if (rv != WAIT_OBJECT_0)
		flog("Exit thread timed out.\n");

	if (d->state == ST_READY) {
		CloseHandle(d->workerthread);
	}

	d->state = ST_DISCONNECTED;
}

/* Look up a DAC by index or unique-ID
 */
dac_t * dac_get(int num) {
	if (num >= 0) {
		dac_t *d = dac_list;
		while (num--) {
			d = d->next;
			if (!d) break;
		}
		return d;
	}
	return NULL;
}

/* Buffer access
 */
struct buffer_item *buf_get_write(dac_t *d) {
	EnterCriticalSection(&d->buffer_lock);
	int write = (d->buffer_read + d->buffer_fullness) % BUFFER_NFRAMES;
	LeaveCriticalSection(&d->buffer_lock);

	flog("M: Writing to index %d\n", write);

	return &d->buffer[write];
}

void buf_advance_write(dac_t *d) {
	EnterCriticalSection(&d->buffer_lock);
	d->buffer_fullness++;
	if (d->state == ST_READY)
		SetEvent(d->loop_go);
	d->state = ST_RUNNING;
	LeaveCriticalSection(&d->buffer_lock);
}

int dac_get_status(dac_t *d) {
	EnterCriticalSection(&d->buffer_lock);
	int fullness = d->buffer_fullness;
	LeaveCriticalSection(&d->buffer_lock);

	if (fullness == BUFFER_NFRAMES) {
		return GET_STATUS_BUSY;
	} else {
		return GET_STATUS_READY;
	}
}

/* Write a frame.
 */

int do_write_frame(dac_t *d, const void * data, int bytes, int pps,
	int reps, int (*convert)(struct buffer_item *, const void *, int)) {

	int points = convert(NULL, NULL, bytes);

	if (reps == ((uint16_t) -1))
		reps = -1;

	/* If not ready for a new frame, bail */
	if (dac_get_status(d) != GET_STATUS_READY) {
		flog("M: NOT READY: %d points, %d reps\n", points, reps);
		return 0;
	}

	/* Ignore 0-repeat frames */
	if (!reps)
		return 1;

	int internal_reps = 250 / points;
	char * bigdata = NULL;
	if (internal_reps) {
		bigdata = malloc(bytes * internal_reps);
		int i;
		for (i = 0; i < internal_reps; i++) {
			memcpy(bigdata + i*bytes, data, bytes);
		}
		bytes *= internal_reps;
		data = bigdata;
	}

	flog("M: Writing: %d/%d points, %d reps\n",
		points, convert(NULL, NULL, bytes), reps);

	struct buffer_item *next = buf_get_write(d);
	convert(next, data, bytes);
	next->pps = pps;
	next->repeatcount = reps;

	buf_advance_write(d);

	if (bigdata) free(bigdata);

	return 1;
}

#include <locale.h>

// This is the buffer filling thread for WriteFrame() and WriteFrameNR()
unsigned __stdcall LoopUpdate(void *dv){
	dac_t *d = (dac_t *)dv;

	int res;
	res = SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_TIME_CRITICAL);
	flog("SetThreadPriority ret %d\n");

	EnterCriticalSection(&d->buffer_lock);

	while (1) {
		/* Wait for us to have data */
		while (d->state == ST_READY) {
			LeaveCriticalSection(&d->buffer_lock);

			flog("L: Waiting...\n");

			int res = WaitForSingleObject(d->loop_go, INFINITE);

			EnterCriticalSection(&d->buffer_lock);
			if (res != WAIT_OBJECT_0) {
				flog("!! WFSO failed: %lu\n",
					GetLastError());
				d->state = ST_BROKEN;
				return 0;
			}
		}

		if (d->state != ST_RUNNING) {
			flog("L: Shutting down.\n");
			LeaveCriticalSection(&d->buffer_lock);
			return 0;
		}

		LeaveCriticalSection(&d->buffer_lock);

		/* Now, see how much data we should write. */
		int cap = 1798;

		cap -= dac_last_status()->buffer_fullness;
		if (cap < 0) cap = 1;

		struct buffer_item *b = &d->buffer[d->buffer_read];

		if (cap < 100) {
			Sleep(5);
			cap += 20;
		}

		/* How many points can we send? */
		int b_left = b->points - b->idx;

		if (cap > b_left)
			cap = b_left;
		if (cap > 1000)
			cap = 1000;


		int res = dac_send_data(&d->conn, b->data + b->idx, cap, b->pps);

		if (res < 0) {
			/* Welp, something's wrong. There's not much we
			 * can do at an API level other than start throwing
			 * "error" returns up to the application... */
			EnterCriticalSection(&d->buffer_lock);
			d->state = ST_BROKEN;
			LeaveCriticalSection(&d->buffer_lock);
			return 1;
		}

		/* What next? */
		EnterCriticalSection(&d->buffer_lock);
		b->idx += res;

		if (b->idx < b->points) {
			/* There's more in this frame. */
			continue;
		}

		b->idx = 0;

		if (b->repeatcount > 1) {
			/* Play this frame again? */
			b->repeatcount--;
		} else if (d->buffer_fullness > 1) {
			/* Move to the next frame */
			flog("L: advancing frame - buffer fullness %d\n", d->buffer_fullness);
			d->buffer_fullness--;
			d->buffer_read++;
			if (d->buffer_read >= BUFFER_NFRAMES)
				d->buffer_read = 0;
		} else if (b->repeatcount >= 0) {
			/* Stop playing until we get a new frame. */
			flog("L: returning to idle\n");
			d->state = ST_READY;
		} else {
			flog("L: repeating frame\n");
		}

		/* If we get here without hitting any of the above cases,
		 * then repeatcount is negative and there's no new frame -
		 * so we're just supposed to keep playing this one again
		 * and again. */ 
	}

	return 0;
}
