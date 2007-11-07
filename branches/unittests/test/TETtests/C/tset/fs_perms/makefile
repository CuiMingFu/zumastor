LIBDIR	= $(TET_ROOT)/lib/tet3
INCDIR	= $(TET_ROOT)/inc/tet3

CFLAGS+= -g -I$(INCDIR)

TARGETS	= fs_perms

all: fs_perms testx.file

fs_perms: fs_perms.o
	$(CC) fs_perms.o -o fs_perms $(CFLAGS) $(LIBDIR)/tcm.o $(LIBDIR)/libapi.a $(LDFLAGS)

testx.file: testx.o
	$(CC) testx.o -o testx.file $(CFLAGS) $(LDFLAGS)

fs_perms.o: fs_perms.c $(INCDIR)/tet_api.h
	$(CC) -c fs_perms.c $(CFLAGS) 

testx.o: testx.c
	$(CC) -c testx.c $(CFLAGS) 

clean:
	rm -f *.o fs_perms testx.file
