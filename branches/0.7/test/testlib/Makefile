TARGETS:=libtest.a simpletest
IPATH:=-Iinclude -I.
OPT_FLAGS:=-g
WARN_FLAGS:=-Wall -Wstrict-prototypes
RM:=rm -f
CFLAGS:=$(OPT_FLAGS) $(WARN_FLAGS) $(IPATH)

all: $(TARGETS)

check: simpletest
	./simpletest

simpletest: simpletest.o libtest.a
	$(CC) -o $@ -L. -ltest simpletest.o

libtest.a: test.o testrunner.o
	ar rc $@ $^
	ranlib $@

clean:
	$(RM) $(TARGETS) *.o *~ semantic.cache include/test/semantic.cache

.PHONY: all clean check

test.o: test.c include/test/test.h testrunner.h
testrunner.o: testrunner.c include/test/test.h testrunner.h
