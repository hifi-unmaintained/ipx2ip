CC=i586-mingw32msvc-gcc
CFLAGS=-O2 -s -Wall
WINDRES=i586-mingw32msvc-windres
LIBS=-lws2_32 -liphlpapi -lrpcrt4
REV=$(shell sh -c 'git rev-parse --short @{0}')

all: wsock32

wsock32: wsock32.c node.c wsock32.def wsock32.rc.in
	sed 's/__REV__/$(REV)/g' wsock32.rc.in | $(WINDRES) -O coff -o wsock32.rc.o
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -s -o wsock32.dll wsock32.c node.c wsock32.def wsock32.rc.o $(LIBS)

clean:
	rm -f wsock32.dll wsock32.rc.o
