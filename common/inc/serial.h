#ifndef SERIAL_H
#define SERIAL_H

#ifndef NULL
#define NULL 0
#endif

#include <string.h>

void debugf(const char *fmt, ...);
void outputf(const char *fmt, ...);
void hexdump(const char * data, int len);
void serial_init();
void serial_send(const char *buf, int len);

static inline void serial_send_str(const char *buf) {
	serial_send(buf, strlen(buf));
}

#endif
