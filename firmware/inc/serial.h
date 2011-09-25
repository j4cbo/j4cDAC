#ifndef SERIAL_H
#define SERIAL_H

#ifndef NULL
#define NULL 0
#endif

void outputf(const char *fmt, ...);
void hexdump(const char * data, int len);
void serial_init();

#endif
