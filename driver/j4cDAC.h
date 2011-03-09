#define J4CDAC_API

#define bool int

#include <stdint.h>

struct EL_Pnt_s {
	uint16_t X;
	uint16_t Y;
	uint8_t	R;
	uint8_t	G;
	uint8_t	B;
	uint8_t	I;
};

struct EAD_Pnt_s {
	int16_t X;
	int16_t Y;
	int16_t R;
	int16_t G;
	int16_t B;
	int16_t I;
	int16_t AL;
	int16_t AR;
};

struct ThreadHandles_s {
	HANDLE ExitThread;
	HANDLE Go;
	HANDLE LoopNotRunning;
	HANDLE ThreadHandle;
};

J4CDAC_API bool __stdcall DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved);

/* EasyLase API */
J4CDAC_API int __stdcall EasyLaseGetCardNum(void);
J4CDAC_API bool __stdcall EasyLaseWriteFrame(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS);
J4CDAC_API bool __stdcall EasyLaseWriteFrameNR(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS, uint16_t Reps);
J4CDAC_API int __stdcall EasyLaseGetLastError(const int CardNum);
J4CDAC_API int __stdcall EasyLaseGetStatus(const int CardNum);
J4CDAC_API bool __stdcall EasyLaseStop(const int CardNum);
J4CDAC_API bool __stdcall EasyLaseClose(void);
J4CDAC_API bool __stdcall EasyLaseWriteDMX(const int CardNum, unsigned char * data);
J4CDAC_API bool __stdcall EasyLaseGetDMX(const int CardNum, unsigned char * data);
J4CDAC_API bool __stdcall EasyLaseDMXOut(const int CardNum, unsigned char * data, uint16_t Baseaddress, uint16_t ChannelCount);
J4CDAC_API bool __stdcall EasyLaseDMXIn(const int CardNum, unsigned char * data, uint16_t Baseaddress, uint16_t ChannelCount);
J4CDAC_API bool __stdcall EasyLaseWriteTTL(const int CardNum, uint16_t TTLValue);
J4CDAC_API bool __stdcall EasyLaseGetDebugInfo(const int CardNum, void * data, uint16_t count);

J4CDAC_API int __stdcall EasyLaseSend(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t KPPS);
J4CDAC_API int __stdcall EasyLaseWriteFrameUncompressed(const int CardNum, const struct EL_Pnt_s* data, int Bytes, uint16_t PPS);
J4CDAC_API int __stdcall EasyLaseReConnect();

/* EzAudDac API */
J4CDAC_API int __stdcall EzAudDacGetCardNum(void);
J4CDAC_API bool __stdcall EzAudDacWriteFrame(const int CardNum, const struct EAD_Pnt_s* data, int Bytes, uint16_t PPS);
J4CDAC_API bool __stdcall EzAudDacWriteFrameNR(const int CardNum, const struct EAD_Pnt_s* data, int Bytes, uint16_t PPS, uint16_t Reps);
J4CDAC_API int __stdcall EzAudDacGetStatus(const int CardNum);
J4CDAC_API bool __stdcall EzAudDacStop(const int CardNum);
J4CDAC_API bool __stdcall EzAudDacClose(void);

/* Common */
#define GET_STATUS_READY	1
#define GET_STATUS_BUSY		2
