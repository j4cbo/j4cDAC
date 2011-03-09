/* j4cDAC driver
 *
 * Copyright 2009 Andrew Kibler
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

#define WINVER 0x0501

#include <stdio.h>
#include <math.h>
#include <string.h>

#include <process.h>
#include <shlwapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "j4cDAC.h"
#include "dac.h"
#include <iniparser.h>

#define BUFFER_POINTS_PER_FRAME	16000
#define BUFFER_NFRAMES		2

#define EXPORT __declspec(dllexport)

/* Double buffer
 */
struct buffer_item {
	struct dac_point data[BUFFER_POINTS_PER_FRAME];
	int points;
	int pps;
	int repeatcount;
	int idx;
};


/* Globals
 */
enum {
	ST_UNINITIALIZED,
	ST_READY,
	ST_RUNNING,
	ST_BROKEN,
	ST_SHUTDOWN
} state = ST_UNINITIALIZED;

static CRITICAL_SECTION buffer_lock;
static HANDLE loop_go;

struct buffer_item dac_buffer[BUFFER_NFRAMES];
int dac_buffer_read, dac_buffer_fullness;
HANDLE ThisDll = NULL, workerthread = NULL;
FILE* fp = NULL;
static dac_conn_t conn;

/* Buffer access
 */
struct buffer_item *buf_get_write() {
	EnterCriticalSection(&buffer_lock);
	int write = (dac_buffer_read + dac_buffer_fullness) % BUFFER_NFRAMES;
	LeaveCriticalSection(&buffer_lock);

	fprintf(fp, "Writing to index %d\n", write);
	fflush(fp);

	return &dac_buffer[write];
}

void buf_advance_write() {
	EnterCriticalSection(&buffer_lock);
	dac_buffer_fullness++;
	if (state == ST_READY)
		SetEvent(loop_go);
	state = ST_RUNNING;
	LeaveCriticalSection(&buffer_lock);
}

/* Data conversion functions
*/
void EasyLase_convert_data(struct buffer_item *buf, const void *vdata, int points) {
	const struct EL_Pnt_s *data = (const struct EL_Pnt_s *)vdata;
	int i;
	if (points > BUFFER_POINTS_PER_FRAME) {
		points = BUFFER_POINTS_PER_FRAME;
	}

	for (i = 0; i < points; i++) {
		buf->data[i].x = (data[i].X - 2048) * 16;
		buf->data[i].y = (data[i].Y - 2048) * 16;
		buf->data[i].r = data[i].R * 256;
		buf->data[i].g = data[i].G * 256;
		buf->data[i].b = data[i].B * 256;
		buf->data[i].i = data[i].I * 256;
		buf->data[i].u1 = 0;
		buf->data[i].u2 = 0;
		buf->data[i].control = 0;
	}

	buf->points = points;
}

void EzAudDac_convert_data(struct buffer_item *buf, const void *vdata, int points) {
	const struct EAD_Pnt_s *data = (const struct EAD_Pnt_s *)vdata;
	int i;
	if (points > BUFFER_POINTS_PER_FRAME) {
		points = BUFFER_POINTS_PER_FRAME;
	}

	for (i = 0; i < points; i++) {
		buf->data[i].x = data[i].X - 32768;
		buf->data[i].y = data[i].Y - 32768;
		buf->data[i].r = data[i].R;
		buf->data[i].g = data[i].G;
		buf->data[i].b = data[i].B;
		buf->data[i].i = data[i].I;
		buf->data[i].u1 = data[i].AL;
		buf->data[i].u2 = data[i].AR;
		buf->data[i].control = 0;
	}

	buf->points = points;
}


/* Stub DllMain function.
 */ 
bool __stdcall DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved) {
	ThisDll = hModule;
	return 1;
}

/* Write a frame.
 */

bool do_write_frame(const int card_num, const void * data, int points, int pps,
	int reps, void (*convert)(struct buffer_item *, const void *, int)) {

	if (reps == ((uint16_t) -1))
		reps = -1;

	fprintf(fp, "Writing frame: %d points, %d reps\n", points, reps);
	fflush(fp);

	/* If not ready for a new frame, bail */
	if (EzAudDacGetStatus(card_num) != GET_STATUS_READY) {
		fprintf(fp, "--> not ready\n");
		fflush(fp);
		return -1;
	}

	/* Ignore 0-repeat frames */
	if (!reps)
		return 0;

	struct buffer_item *next = buf_get_write();
	convert(next, data, points);
	next->pps = pps;
	next->repeatcount = reps;

	buf_advance_write();
	return 0;
}

bool __stdcall EzAudDacWriteFrameNR(const int CardNum, const struct EAD_Pnt_s* data, int Bytes, uint16_t PPS, uint16_t Reps){
	return do_write_frame(CardNum, data, Bytes / sizeof(*data),
		PPS, Reps, EzAudDac_convert_data);
}

bool __stdcall EzAudDacWriteFrame(const int CardNum, const struct EAD_Pnt_s* data, int Bytes, uint16_t PPS){
	return EzAudDacWriteFrameNR(CardNum, data, Bytes, PPS, -1);
}

EXPORT bool __stdcall EasyLaseWriteFrameNR(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS, uint16_t Reps){
	return do_write_frame(CardNum, data, Bytes / sizeof(*data),
		PPS, Reps, EasyLase_convert_data);
}

EXPORT bool __stdcall EasyLaseWriteFrame(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS){
	return EasyLaseWriteFrameNR(CardNum, data, Bytes, PPS, -1);
}

/* Get the output status.
 */
EXPORT int __stdcall EzAudDacGetStatus(const int CardNum){
	EnterCriticalSection(&buffer_lock);
	int fullness = dac_buffer_fullness;
	LeaveCriticalSection(&buffer_lock);

	if (fullness == BUFFER_NFRAMES) {
		fprintf(fp, "bouncing\n");
		fflush(fp);
		Sleep(1);
		return GET_STATUS_BUSY;
	} else {
		return GET_STATUS_READY;
	}
}

#include <locale.h>

// This is the buffer filling thread for WriteFrame() and WriteFrameNR()
unsigned __stdcall LoopUpdate(void *x){
	EnterCriticalSection(&buffer_lock);

	while (1) {
		/* Wait for us to have data */
		while (state == ST_READY) {
			LeaveCriticalSection(&buffer_lock);

			fprintf(fp, "Waiting...\n");
			fflush(fp);

			int res = WaitForSingleObject(loop_go, INFINITE);

			EnterCriticalSection(&buffer_lock);
			if (res != WAIT_OBJECT_0) {
				fprintf(fp, "!! WFSO failed: %lu\n",
					GetLastError());
				state = ST_BROKEN;
				return 0;
			}
		}

		if (state != ST_RUNNING) {
			fprintf(fp, "-- Shutting down.\n");
			fflush(fp);
			LeaveCriticalSection(&buffer_lock);
			return 0;
		}

		LeaveCriticalSection(&buffer_lock);

		/* Now, see how much data we should write. */
		int cap = 1799 - dac_last_status()->buffer_fullness;

		struct buffer_item *b = &dac_buffer[dac_buffer_read];

		/* How many points can we send? */
		int b_left = b->points - b->idx;
		if (cap > b_left)
			cap = b_left;
		if (cap > 1000)
			cap = 1000;

		if (cap < 100) {
			Sleep(10);
			cap += 20;
		}

		fprintf(fp, "Writing %d points (buf has %d)\n", cap, dac_last_status()->buffer_fullness);
		fflush(fp);

		if (dac_send_data(&conn, b->data + b->idx, cap, b->pps) < 0) {
			/* Welp, something's wrong. There's not much we
			 * can do at an API level other than start throwing
			 * "error" returns up to the application... */
			EnterCriticalSection(&buffer_lock);
			state = ST_BROKEN;
			LeaveCriticalSection(&buffer_lock);
			return 1;
		}

		/* What next? */
		EnterCriticalSection(&buffer_lock);
		b->idx += cap;

		if (b->idx < b->points) {
			/* There's more in this frame. */
			continue;
		}

		b->idx = 0;

		if (b->repeatcount > 1) {
			/* Play this frame again? */
			b->repeatcount--;
		} else if (dac_buffer_fullness) {
			/* Move to the next frame */
			dac_buffer_fullness--;
			dac_buffer_read++;
			if (dac_buffer_read >= BUFFER_NFRAMES)
				dac_buffer_read = 0;
		} else if (b->repeatcount >= 0) {
			/* Stop playing until we get a new frame. */
			state = ST_READY;
		}

		/* If we get here without hitting any of the above cases,
		 * then repeatcount is negative and there's no new frame -
		 * so we're just supposed to keep playing this one again
		 * and again. */ 
	}

	return 0;
}
//============================================================================

EXPORT int __stdcall EzAudDacGetCardNum(void){
	if (state != ST_UNINITIALIZED) {
		EzAudDacStop(0);
		EzAudDacClose();
	}

	char dll_fn[MAX_PATH] = { 0 };
	GetModuleFileName((HMODULE)ThisDll, dll_fn, sizeof(dll_fn)-1);
	PathRemoveFileSpec(dll_fn);

	char fn[MAX_PATH];

	/* Log file */
	if (!fp) {
		snprintf(fn, sizeof(fn), "%s\\j4cDAC.txt", dll_fn);
		fp = fopen(fn, "w");
	}

	fprintf(fp, "== j4cDAC ==\n");

	/* Load up the INI */
	snprintf(fn, sizeof(fn), "%s\\j4cDAC.ini", dll_fn);
	fprintf(fp, "Loading file: %s\n", fn);
	fflush(fp);
	dictionary *d = iniparser_load(fn);

	if (!d) {
		fprintf(fp,"Failed to open ini file.\n");
		fflush(fp);
		return 0;
	}

	// Initialize Winsock
	WSADATA wsaData;
	int res = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (res != 0) {
		fprintf(fp, "!! WSAStartup failed: %d\n", res);
		fflush(fp);
		return 0;
	}

	// Parse ini
	const char * host = iniparser_getstring(d, "network:host", NULL);
	if (!host) {
		fprintf(fp, "!! No host given in config file.\n");
		fflush(fp);
		return 0;
	}
	//char * host = strdup(hosts);

	const char * port = iniparser_getstring(d, "network:port", "7654");

	// Connect to the DAC
	if (dac_connect(&conn, host, port) < 0) {
//		free(host);
		fprintf(fp, "!! DAC connection failed.\n");
		fflush(fp);
		iniparser_freedict(d);
		return 0;
	}

	iniparser_freedict(d);
	//free(host);

	// Fire off the worker thread
	InitializeCriticalSection(&buffer_lock);
	loop_go = CreateEvent(NULL, FALSE, FALSE, NULL);
	state = ST_READY;

	workerthread = (HANDLE)_beginthreadex(NULL, 0, &LoopUpdate, NULL, 0, NULL);
	if (!workerthread) {
		fprintf(fp, "!! Begin thread error: %s\n", strerror(errno));
		return 0;
	} else {
		SetThreadPriority(workerthread, THREAD_PRIORITY_TIME_CRITICAL);
	}

	fprintf(fp, "Ready.\n");
	fflush(fp);

	return 1;
}

bool __stdcall EzAudDacStop(const int CardNum){
	EnterCriticalSection(&buffer_lock);
	if (state == ST_READY)
		SetEvent(loop_go);
	else
		state = ST_READY;
	LeaveCriticalSection(&buffer_lock);

	return 0;
}

bool __stdcall EzAudDacClose(void){
	EnterCriticalSection(&buffer_lock);
	if (state == ST_READY)
		SetEvent(loop_go);
	state = ST_SHUTDOWN;
	LeaveCriticalSection(&buffer_lock);

	int rv = WaitForSingleObject(workerthread, 250);
	if (rv != WAIT_OBJECT_0)
		fprintf(fp," Exit thread timed out.\n");

	CloseHandle(workerthread);

	if (fp) {
		fprintf(fp,"Exiting\n");
		fclose(fp);
		fp = NULL;
	}

	return 0;
}

/****************************************************************************/

/* Wrappers and stubs
 */
EXPORT int __stdcall EasyLaseGetCardNum(void) {
	return EzAudDacGetCardNum();
}

EXPORT int __stdcall EasyLaseSend(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t KPPS) {
	fprintf(fp, "ELSend called: card %d, %d bytes, %d kpps\n", CardNum, Bytes, KPPS);
	fflush(fp);
	return 1;
}

EXPORT int __stdcall EasyLaseWriteFrameUncompressed(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS) {
	fprintf(fp," ELWFU called: card %d, %d bytes, %d pps\n", CardNum, Bytes, PPS);
	fflush(fp);
	return 1;
}

EXPORT int __stdcall EasyLaseReConnect() {
	fprintf(fp," ELReConnect called.\n");
	fflush(fp);
	return 0;
}

EXPORT int __stdcall EasyLaseGetLastError(const int CardNum) {
	return 0;
}

EXPORT int __stdcall EasyLaseGetStatus(const int CardNum) {
	return EzAudDacGetStatus(CardNum);
}

EXPORT bool __stdcall EasyLaseStop(const int CardNum) {
	return EzAudDacStop(CardNum);
}

EXPORT bool __stdcall EasyLaseClose(void) {
	return EzAudDacClose();
}

EXPORT bool __stdcall EasyLaseWriteDMX(const int CardNum, unsigned char * data) {
	return -1;
}

EXPORT bool __stdcall EasyLaseGetDMX(const int CardNum, unsigned char * data) {
	return -1;
}

EXPORT bool __stdcall EasyLaseDMXOut(const int CardNum, unsigned char * data, uint16_t Baseaddress, uint16_t ChannelCount) {
	return -1;
}

EXPORT bool __stdcall EasyLaseDMXIn(const int CardNum, unsigned char * data, uint16_t Baseaddress, uint16_t ChannelCount) {
	return -1;
}

EXPORT bool __stdcall EasyLaseWriteTTL(const int CardNum, uint16_t TTLValue) {
	return -1;
}

EXPORT bool __stdcall EasyLaseGetDebugInfo(const int CardNum, void * data, uint16_t count) {
	return -1;
}
