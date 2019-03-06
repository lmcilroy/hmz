CFLAGS=-Wall -Werror -Wcast-align -Wstrict-overflow -Wstrict-aliasing -Wextra -Wpedantic -Wshadow -O3 -march=native -falign-loops=4 # -DDEBUG=1

all:	hmz

hmz:	hmz.o hmzencode.o hmzdecode.o

hmz.o:	hmz.c hmz.h

hmzencode.o:	hmzencode.c hmz.h hmz_int.h

hmzdecode.o:	hmzdecode.c hmz.h hmz_int.h

clean:
	rm -f hmz *.o
