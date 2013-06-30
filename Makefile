CC=i686-w64-mingw32-gcc
CFLAGS=-std=c99 -O2 -s -Wall -fno-strict-aliasing -D_DEBUG
WINDRES=i686-w64-mingw32-windres
LIBS=-lws2_32 -lmsvcrt
REV=$(shell sh -c 'git rev-parse --short @{0}')

all: wsock32

wsock32: wsock32.c wsock32.def wsock32.rc.in
	sed 's/__REV__/$(REV)/g' wsock32.rc.in | $(WINDRES) -O coff -o wsock32.rc.o
	$(CC) $(CFLAGS) -nostdlib -Wl,--enable-stdcall-fixup -shared -s -o wsock32.dll wsock32.c wsock32.def wsock32.rc.o $(LIBS)

clean:
	rm -f wsock32.dll wsock32.rc.o
