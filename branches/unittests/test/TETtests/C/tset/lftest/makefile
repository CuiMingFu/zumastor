LIBDIR	= $(TET_ROOT)/lib/tet3
INCDIR	= $(TET_ROOT)/inc/tet3

CFLAGS+= -g -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -I$(INCDIR)
SRCS=$(wildcard *.c)
TARGETS	= lftest

all: lftest

lftest: lftest.o
	$(CC) lftest.o -o lftest $(CFLAGS) $(LIBDIR)/tcm.o $(LIBDIR)/libapi.a $(LDFLAGS)

lftest.o: lftest.c $(INCDIR)/tet_api.h
	$(CC) -c lftest.c $(CFLAGS) 

clean:
	rm -f *.o lftest
