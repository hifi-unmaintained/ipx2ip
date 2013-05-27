CC=i686-w64-mingw32-gcc
CFLAGS=-std=c99 -O2 -s -Wall -fno-strict-aliasing
WINDRES=i686-w64-mingw32-windres
LIBS=-lws2_32 -liphlpapi
REV=$(shell sh -c 'git rev-parse --short @{0}')

all: wsock32

wsock32: wsock32.c wsock32.def wsock32.rc.in thipx32.c
	sed 's/__REV__/$(REV)/g' wsock32.rc.in | $(WINDRES) -O coff -o wsock32.rc.o
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -s -o wsock32.dll wsock32.c thipx32.c wsock32.def wsock32.rc.o $(LIBS)

clean:
	rm -f wsock32.dll wsock32.rc.o
