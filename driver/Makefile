j4cDAC.dll: j4cDAC.c comm.c
	i586-mingw32msvc-gcc -shared j4cDAC.c comm.c ../../iniparser/src/*.c -o j4cDAC.dll  -I../../iniparser/src -I../common -Os -Wall -g -lshlwapi -lws2_32 -Wl,--kill-at

lol.dll: lol.cpp
	i586-mingw32msvc-g++ -shared lol.cpp -o lol.dll -Os -Wall
