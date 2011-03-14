enum dfu_state {
	appIDLE = 0,
	appDETACH = 1,
	dfuIDLE = 2,
	dfuDNLOAD_SYNC = 3,
	dfuDNBUSY = 4,
	dfuDNLOAD_IDLE = 5,
	dfuMANIFEST_SYNC = 6,
	dfuMANIFEST = 7,
	dfuMANIFEST_WAIT_RESET = 8,
	dfuUPLOAD_IDLE = 9,
	dfuERROR = 10
};

enum dfu_status {
	OK = 0,
	errTARGET = 1,
	errFILE = 2,
	errWRITE = 3,
	errERASE = 4,
	errCHECK_ERASED = 5,
	errPROG = 6,
	errVERIFY = 7,
	errADDRESS = 8,
	errNOTDONE = 9,
	errFIRMWARE = 10,
	errVENDOR = 11,
	errUSBR = 12,
	errPOR = 13,
	errUNKNOWN = 14,
	errSTALLEDPKT = 15,
};
