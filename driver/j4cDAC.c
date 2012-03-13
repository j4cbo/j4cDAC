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

#if 0
#include <iniparser.h>
#endif

#define EXPORT __declspec(dllexport)

/* Globals
 */

static CRITICAL_SECTION dac_list_lock;
dac_t * dac_list = NULL;

HANDLE ThisDll = NULL, workerthread = NULL, watcherthread = NULL;
FILE* fp = NULL;
char dll_fn[MAX_PATH] = { 0 };
int fucked = 0;
int time_to_go = 0;
struct timeval load_time;
LARGE_INTEGER timer_start, timer_freq;


/* Subtract the struct timeval values x and y, storing the result in result.
 * Return 1 if the difference is negative, otherwise 0.
 *
 * From the glibc manual. */
     
int timeval_subtract (struct timeval *res, struct timeval *x,
		struct timeval *y) {
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < y->tv_usec) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_usec - y->tv_usec > 1000000) {
		int nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}

	/* Compute the time remaining. tv_usec is certainly positive. */
	res->tv_sec = x->tv_sec - y->tv_sec;
	res->tv_usec = x->tv_usec - y->tv_usec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

/* Logging
 */
void flog (char *fmt, ...) {
	va_list args;
	va_start(args, fmt);

	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);

	LONGLONG time_diff = time.QuadPart - timer_start.QuadPart;
	LONGLONG v = (time_diff * 1000000) / timer_freq.QuadPart;

	fprintf(fp, "[%d.%06d] ", (int)(v / 1000000), (int)(v % 1000000));
	vfprintf(fp, fmt, args);
	va_end(args);
}

void dac_list_insert(dac_t *d) {
	EnterCriticalSection(&dac_list_lock);
	d->next = dac_list;
	dac_list = d;
	LeaveCriticalSection(&dac_list_lock);
}

unsigned __stdcall FindDACs(void *_bogus) {
	SOCKET sock;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) {
		log_socket_error(NULL, "socket");
		return 1;
	}

	int opt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) < 0) {
		log_socket_error(NULL, "setsockopt SO_REUSEADDR");
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(7654);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_socket_error(NULL, "bind");
		return 1;
	}

	flog("_: listening for DACs...\n");

	while(!time_to_go) {
		struct sockaddr_in src;
		struct dac_broadcast buf;
		int srclen = sizeof(src);
		int len = recvfrom(sock, (char *)&buf, sizeof(buf), 0, (struct sockaddr *)&src, &srclen);
		if (len < 0) {
			log_socket_error(NULL, "recvfrom");
			return 1;
		}

		/* See if this is a DAC we already knew about */
		EnterCriticalSection(&dac_list_lock);
		dac_t *p = dac_list;
		while (p) {
			if (p->addr.s_addr == src.sin_addr.s_addr) break;
			p = p->next;
		}

		if (p && (p->addr.s_addr == src.sin_addr.s_addr)) {
			LeaveCriticalSection(&dac_list_lock);
			continue;
		}

		LeaveCriticalSection(&dac_list_lock);

		/* Make a new DAC entry */
		dac_t * new_dac = malloc(sizeof(dac_t));
		if (!new_dac) {
			flog("!! malloc(sizeof(dac_t)) failed\n");
			continue;
		}
		dac_init(new_dac);
		new_dac->addr = src.sin_addr;
		memcpy(new_dac->mac_address, buf.mac_address, 6);
		new_dac->dac_id = (1 << 31) | (buf.mac_address[3] << 16) \
			| (buf.mac_address[4] << 8) | buf.mac_address[5];

		char host[40];
		strncpy(host, inet_ntoa(src.sin_addr), sizeof(host) - 1);
		host[sizeof(host) - 1] = 0;

		flog("_: Found new DAC: %s\n", inet_ntoa(src.sin_addr));

		new_dac->state = ST_DISCONNECTED;
		dac_list_insert(new_dac);
	}

	flog("_: Exiting\n");

	return 0;
}

/* Stub DllMain function.
 */ 
bool __stdcall DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved) {
	ThisDll = hModule;

	fucked = 0;

	if (reason == DLL_PROCESS_ATTACH) {
		GetModuleFileName((HMODULE)ThisDll, dll_fn, sizeof(dll_fn)-1);
		PathRemoveFileSpec(dll_fn);

		char fn[MAX_PATH];

		QueryPerformanceFrequency(&timer_freq);
		QueryPerformanceCounter(&timer_start);

		/* Log file */
		if (!fp) {
			snprintf(fn, sizeof(fn), "%s\\j4cDAC.txt", dll_fn);
			fp = fopen(fn, "a");
			flog("== DLL Loaded ==\n");
		}

		// Initialize Winsock
		WSADATA wsaData;
		int res = WSAStartup(MAKEWORD(2,2), &wsaData);
		if (res != 0) {
			flog("!! WSAStartup failed: %d\n", res);
			fucked = 1;
		}

		InitializeCriticalSection(&dac_list_lock);
		time_to_go = 0;

		// Start up a thread looking for broadcasts
		watcherthread = (HANDLE)_beginthreadex(NULL, 0, &FindDACs, NULL, 0, NULL);
		if (!watcherthread) {
			flog("!! BeginThreadEx error: %s\n", strerror(errno));
			fucked = 1;
		}

		gettimeofday(&load_time, NULL);

		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

		DWORD pc = GetPriorityClass(GetCurrentProcess());
		flog("Process priority class: %d\n", pc);
		if (!pc) flog("Error: %d\n", GetLastError());
#if 0
	char fn[MAX_PATH];
	/* Load up the INI */
	snprintf(fn, sizeof(fn), "%s\\j4cDAC.ini", dll_fn);
	flog("Loading file: %s\n", fn);
	dictionary *d = iniparser_load(fn);

	if (!d) {
		flog("Failed to open ini file.\n");
		return 0;
	}

	// Parse ini
	const char * host = iniparser_getstring(d, "network:host", NULL);
	if (!host) {
		flog("!! No host given in config file.\n");
		return 0;
	}
	//char * host = strdup(hosts);

	const char * port = iniparser_getstring(d, "network:port", "7654");

	dac_t * new_dac = malloc(sizeof(dac_t));
	dac_init(new_dac);

	if (dac_open_connection(new_dac, host, port) < 0) {
		iniparser_freedict(d);
		return 0;
	}

	iniparser_freedict(d);

	flog("Ready.\n");

#endif

	} else if (reason == DLL_PROCESS_DETACH) {
		WSACleanup();
		DeleteCriticalSection(&dac_list_lock);

		timeEndPeriod(1);

		if (fp) {
			flog("== DLL Unloaded ==\n");
			fclose(fp);
			fp = NULL;
		}
	}
	return 1;
}

/* Get the output status.
 */
EXPORT int __stdcall EtherDreamGetStatus(const int *CardNum) {
	dac_t *d = dac_get(*CardNum);
	if (!d) {
		flogd(d, "M: GetStatus(%d) return -1\n", *CardNum);
		return -1;
	}

	int st = dac_get_status(d);
	if (st == GET_STATUS_BUSY) {
		flogd(d, "M: bouncing\n");
		Sleep(2);
	}

	return st;
}

static void do_close(void) {
	EnterCriticalSection(&dac_list_lock);
	dac_t *d = dac_list;
	while (d) {
		if (d->state != ST_DISCONNECTED)
			dac_close_connection(d);
		d = d->next;
	}
	LeaveCriticalSection(&dac_list_lock);
}

EXPORT int __stdcall EtherDreamGetCardNum(void){

	flog("== EtherDreamGetCardNum ==\n");

	/* Clean up any already opened DACs */
	do_close();

	/* Gross-vile-disgusting-sleep for up to a bit over a second to
	 * catch broadcast packets from DACs */
	struct timeval tv, tv_diff;
	gettimeofday(&tv, NULL);
	timeval_subtract(&tv_diff, &tv, &load_time);
	int ms_left = 1100 - ((tv_diff.tv_sec * 1000) + 
	                      (tv_diff.tv_usec / 1000));
	flog("Waiting %d milliseconds.\n", ms_left);
	Sleep(ms_left);

	/* Count how many DACs we have. */
	int count = 0;

	EnterCriticalSection(&dac_list_lock);
	dac_t *d = dac_list;
	while (d) {
		d = d->next;
		count++;
	}
	LeaveCriticalSection(&dac_list_lock);

	flog("Found %d DACs.\n", count);

	return count;
}

EXPORT bool __stdcall EtherDreamStop(const int *CardNum){
	dac_t *d = dac_get(*CardNum);
	flogd(d, "== Stop(%d) ==\n", *CardNum);
	if (!d) return 0;
	EnterCriticalSection(&d->buffer_lock);
	if (d->state == ST_READY)
		SetEvent(d->loop_go);
	else
		d->state = ST_READY;
	LeaveCriticalSection(&d->buffer_lock);

	return 0;
}

EXPORT bool __stdcall EtherDreamClose(void){
	flog("== Close ==\n");
	time_to_go = 1;
	WSACancelBlockingCall();
	if (WaitForSingleObject(watcherthread, 1000) == WAIT_TIMEOUT) {
		flog("!! watcher thread timed out\n");
	}
	do_close();
	return 0;

}

EXPORT bool __stdcall EtherDreamOpenDevice(const int *CardNum) {
	dac_t *d = dac_get(*CardNum);
	if (!d) return 0;
	if (dac_open_connection(d) < 0) return 0;
	return 1;
}

EXPORT bool __stdcall EtherDreamCloseDevice(const int *CardNum) {
	dac_t *d = dac_get(*CardNum);
	if (!d) return 0;
	dac_close_connection(d);
	return 1;
}

/****************************************************************************/

/* Data conversion functions
*/
int EasyLase_convert_data(struct buffer_item *buf, const void *vdata, int bytes) {
	const struct EL_Pnt_s *data = (const struct EL_Pnt_s *)vdata;
	int points = bytes / sizeof(*data);
	int i;

	if (!buf) return points;
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
	return points;
}

int EzAudDac_convert_data(struct buffer_item *buf, const void *vdata, int bytes) {
	const struct EAD_Pnt_s *data = (const struct EAD_Pnt_s *)vdata;
	int points = bytes / sizeof(*data);
	int i;

	if (!buf) return points;
	if (points > BUFFER_POINTS_PER_FRAME) {
		points = BUFFER_POINTS_PER_FRAME;
	}

	for (i = 0; i < points; i++) {
		buf->data[i].x = data[i].X; //  - 32768;
		buf->data[i].y = data[i].Y; //  - 32768;
		buf->data[i].r = data[i].R << 1;
		buf->data[i].g = data[i].G << 1;
		buf->data[i].b = data[i].B << 1;
		buf->data[i].i = data[i].I << 1;
		buf->data[i].u1 = data[i].AL;
		buf->data[i].u2 = data[i].AR;
		buf->data[i].control = 0;
	}

	buf->points = points;
	return points;
}

EXPORT bool __stdcall EtherDreamWriteFrame(const int *CardNum, const struct EAD_Pnt_s* data,
		int Bytes, uint16_t PPS, uint16_t Reps) {
	return EzAudDacWriteFrameNR(CardNum, data, Bytes, PPS, Reps);
}

EXPORT bool __stdcall EzAudDacWriteFrameNR(const int *CardNum, const struct EAD_Pnt_s* data,
		int Bytes, uint16_t PPS, uint16_t Reps) {
	dac_t *d = dac_get(*CardNum);
	if (!d) return 0;
	return do_write_frame(d, data, Bytes, PPS, Reps, EzAudDac_convert_data);
}

EXPORT bool __stdcall EzAudDacWriteFrame(const int *CardNum, const struct EAD_Pnt_s* data,
		int Bytes, uint16_t PPS) {
	return EzAudDacWriteFrameNR(CardNum, data, Bytes, PPS, -1);
}

EXPORT bool __stdcall EasyLaseWriteFrameNR(const int *CardNum, const struct EL_Pnt_s* data,
		int Bytes, uint16_t PPS, uint16_t Reps) {
	dac_t *d = dac_get(*CardNum);
	if (!d) return 0;
	return do_write_frame(d, data, Bytes, PPS, Reps, EasyLase_convert_data);
}

EXPORT bool __stdcall EasyLaseWriteFrame(const int *CardNum, const struct EL_Pnt_s* data,
		int Bytes, uint16_t PPS) {
	return EasyLaseWriteFrameNR(CardNum, data, Bytes, PPS, -1);
}

EXPORT void __stdcall EtherDreamGetDeviceName(const int *CardNum, char *buf, int max) {
	if (max <= 0) return;
	buf[0] = 0;
	dac_t *d = dac_get(*CardNum);
	if (!d) return;
	dac_get_name(d, buf, max);
}

/****************************************************************************/

/* Wrappers and stubs
 */
EXPORT int __stdcall EzAudDacGetCardNum(void) {

	flog("== EzAudDacGetCardNum ==\n");

	/* Clean up any already opened DACs */
	do_close();

	/* Gross-vile-disgusting-sleep for up to a bit over a second to
	 * catch broadcast packets from DACs */
	struct timeval tv, tv_diff;
	gettimeofday(&tv, NULL);
	timeval_subtract(&tv_diff, &tv, &load_time);
	int ms_left = 1100 - ((tv_diff.tv_sec * 1000) + 
	                      (tv_diff.tv_usec / 1000));
	flog("Waiting %d milliseconds.\n", ms_left);
	Sleep(ms_left);

	/* Count how many DACs we have. Along the way, open them */
	int count = 0;

	EnterCriticalSection(&dac_list_lock);
	dac_t *d = dac_list;
	while (d) {
		dac_open_connection(d);
		d = d->next;
		count++;
	}
	LeaveCriticalSection(&dac_list_lock);

	flog("Found %d DACs.\n", count);

	return count;
}

EXPORT int __stdcall EasyLaseGetCardNum(void) {
	return EzAudDacGetCardNum();
}

EXPORT int __stdcall EasyLaseSend(const int *CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t KPPS) {
	flog("ELSend called: card %d, %d bytes, %d kpps\n", *CardNum, Bytes, KPPS);
	return 1;
}

EXPORT int __stdcall EasyLaseWriteFrameUncompressed(const int *CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS) {
	flog(" ELWFU called: card %d, %d bytes, %d pps\n", *CardNum, Bytes, PPS);
	return 1;
}

EXPORT int __stdcall EasyLaseReConnect() {
	flog(" ELReConnect called.\n");
	return 0;
}

EXPORT int __stdcall EasyLaseGetLastError(const int *CardNum) {
	return 0;
}

EXPORT int __stdcall EzAudDacGetStatus(const int *CardNum) {
	return EtherDreamGetStatus(CardNum);
}

EXPORT int __stdcall EasyLaseGetStatus(const int *CardNum) {
	return EtherDreamGetStatus(CardNum);
}

EXPORT bool __stdcall EzAudDacStop(const int *CardNum) {
	return EtherDreamStop(CardNum);
}

EXPORT bool __stdcall EasyLaseStop(const int *CardNum) {
	return EtherDreamStop(CardNum);
}

EXPORT bool __stdcall EzAudDacClose(void) {
	return EtherDreamClose();
}

EXPORT bool __stdcall EasyLaseClose(void) {
	return EtherDreamClose();
}

EXPORT bool __stdcall EasyLaseWriteDMX(const int *CardNum, unsigned char * data) {
	return -1;
}

EXPORT bool __stdcall EasyLaseGetDMX(const int *CardNum, unsigned char * data) {
	return -1;
}

EXPORT bool __stdcall EasyLaseDMXOut(const int *CardNum, unsigned char * data, uint16_t Baseaddress, uint16_t ChannelCount) {
	return -1;
}

EXPORT bool __stdcall EasyLaseDMXIn(const int *CardNum, unsigned char * data, uint16_t Baseaddress, uint16_t ChannelCount) {
	return -1;
}

EXPORT bool __stdcall EasyLaseWriteTTL(const int *CardNum, uint16_t TTLValue) {
	return -1;
}

EXPORT bool __stdcall EasyLaseGetDebugInfo(const int *CardNum, void * data, uint16_t count) {
	return -1;
}
