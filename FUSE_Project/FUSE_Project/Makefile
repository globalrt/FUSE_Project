# Ubuntu 14.04.3 LTS / gcc 4.8.4 / fusermount 2.9.2 / libfuse-dev

CC=gcc
LD=ld
RM=rm
CFLAGS=-std=gnu99 -O3 -D_FILE_OFFSET_BITS=64 -lfuse

EXE=asdfs
SRCS=asdfs_internal.c asdfs.c main.c

all: 
	$(CC) $(SRCS) -o $(EXE) $(CFLAGS)

clean:
	$(RM) -f *.o $(EXE)
