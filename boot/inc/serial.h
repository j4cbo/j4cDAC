#ifndef SERIAL_H
#define SERIAL_H

void outputf(const char *fmt, ...);
void noutputf(const char *fmt, ...);
void hexdump(const char * data, int len);
void serial_init();

#endif
