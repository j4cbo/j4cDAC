EtherDream.dll: j4cDAC.c comm.c dac.c dac.h
	i586-mingw32msvc-gcc -shared j4cDAC.c comm.c dac.c ../../iniparser/src/*.c -o EtherDream.dll  -I../../iniparser/src -I../common -Os -Wall -g -lshlwapi -lws2_32 -lwinmm -Wl,--kill-at
