CC = gcc
CFLAGS = -I../../common -Wall -Wextra -ansi -pedantic -std=c99

all: test etherdream.dylib

test: etherdream.c etherdream.h test.c
	$(CC) $(CFLAGS) -g etherdream.c test.c -o $@

etherdream.dylib: etherdream.c
	gcc $(CFLAGS) -dynamiclib etherdream.c -o etherdream.dylib
