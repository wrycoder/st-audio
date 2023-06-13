CC := /usr/bin/i686-w64-mingw32-gcc

CFLAGS := -I /home/ubuntu/x86_64/include -fopenmp -static -DUNICODE -Wno-incompatible-pointer-types

LDFLAGS := -L /home/ubuntu/x86_64/lib -lsox -lwinmm -lole32 -luuid -lshell32 -fopenmp -static -mwindows

splice.exe: splice.c splice.h
	$(CC) $(CFLAGS) splice.c $(LDFLAGS) -o splice.exe

clean:
	rm splice.exe
