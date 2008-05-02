INCLUDES +=-I.. -I$(kernel) -I../../test/testlib/include 
CPPFLAGS +=-D_FILE_OFFSET_BITS=64 -DDDSNAP_MEM_MONITOR=30 $(INCLUDES)
CFLAGS +=-g -Wall -std=gnu99
CFLAGS +=-fprofile-arcs -ftest-coverage #enable coverage tracking
LDFLAGS +=$(CFLAGS) -L../../test/testlib -ltest
VALGRIND_FLAGS +=--trace-children=yes

kernel =../kernel
testsuites =./testddsnap

.PHONY: all
all:

.PHONY: quickcheck check check-coverage

check-coverage:
	$(MAKE) -e quickcheck
	gcov -a testddsnap.c
	cat ddsnap.c.gcov

check: $(testsuites)
	for test in $(testsuites) ; do valgrind $(VALGRIND_FLAGS) $$test ; done

check-leaks: $(testsuites)
	for test in $(testsuites) ; do valgrind --leak-check=full --show-reachable=yes $$test ; done

quickcheck: $(testsuites)
	for test in $(testsuites) ; do $$test ; done

testddsnap: testddsnap.o ../buffer.o ../ddsnapd.o ../event.o ../ddsnap.agent.o ../xdelta/xdelta3.o ../delta.o ../diskio.o ../daemonize.o
	$(CC) $(LDFLAGS) -lc -lpopt -lz -o $@ $^

.PHONY: check quickcheck check-coverage tests

testddsnap.o:  testddsnap.c ../../test/testlib/include/test/test.h ../ddsnap.c ../kernel/dm-ddsnap.h ../buffer.h ../list.h ../daemonize.h ../ddsnap.h ../event.h ../ddsnap.agent.h ../delta.h ../diskio.h ../sock.h ../trace.h ../build.h

clean:
	rm -f testddsnap.o $(testsuites) *.gcov *.gcno *.gcda

.PHONY: clean
