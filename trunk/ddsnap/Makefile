PREFIX ?=
BINDIR ?=sbin
MANDIR ?=usr/share/man

kernel=./kernel
INSTDIR=$(PREFIX)/$(BINDIR)
INCLUDES +=-I$(kernel)
CPPFLAGS +=-D_FILE_OFFSET_BITS=64 -DDSNAP_MEM_MONITOR=30 $(INCLUDES)
CFLAGS +=-g -Wall -std=gnu99 -O2 -fno-strict-aliasing

deps = Makefile trace.h diskio.h buffer.h list.h sock.h
ddsnap_agent_deps = $(deps) ddsnap.h ddsnap.agent.h $(kernel)/dm-ddsnap.h daemonize.h
ddsnapd_deps = $(deps) $(kernel)/dm-ddsnap.h daemonize.h ddsnap.h
ddsnap_deps = $(deps) ddsnap.h ddsnap.agent.h $(kernel)/dm-ddsnap.h
testdir = tests

ddsnap_binaries = ddsnap
binaries = $(ddsnap_binaries) devspam nblock_write ddsnap-sb

all: $(binaries) ddsnap.8.gz nblock_write.8.gz ddsnap-sb.8.gz
.PHONY: all

clean:
	$(MAKE) -C $(testdir) clean
	rm -f build.h $(binaries) *.o xdelta/*.o a.out *.gz patches/*/AUTO.* test-snapstore test-origin
.PHONY: clean

install:
	mkdir -p $(INSTDIR) || true
	install -s -v $(binaries) $(INSTDIR)
	install -v --mode=644 ddsnap.8.gz nblock_write.8.gz ddsnap-sb.8.gz $(PREFIX)/$(MANDIR)/man8/
.PHONY: install

genpatches:
	./scripts/genallpatches.sh

.PHONY: check-coverage check quickcheck check-leaks
check-coverage: all
	$(MAKE) -C $(testdir) check-coverage
check: all
	$(MAKE) -C $(testdir) check
quickcheck: all
	$(MAKE) -C $(testdir) quickcheck
check-leaks: all
	$(MAKE) -C $(testdir) check-leaks

xdelta/xdelta3.o: xdelta/xdelta3.c Makefile xdelta/xdelta3.h xdelta/xdelta3-list.h xdelta/xdelta3-cfgs.h

delta.o: delta.c Makefile delta.h xdelta/xdelta3.h

ddsnap.agent.o: ddsnap.agent.c $(ddsnap_agent_deps)

ddsnapd.o: ddsnapd.c $(ddsnapd_deps)

diskio.o: diskio.c Makefile trace.h diskio.h

buffer.o: buffer.c $(deps)

daemonize.o: daemonize.c $(deps)

nblock_write: nblock_write.c
	$(CC) nblock_write.c -o nblock_write

ddsnap: ddsnap.c ddsnapd.o buffer.o ddsnap.agent.o xdelta/xdelta3.o delta.o diskio.o daemonize.o $(ddsnap_deps) build.h
	$(CC) ddsnap.c $(CFLAGS) $(CPPFLAGS) buffer.o ddsnapd.o ddsnap.agent.o xdelta/xdelta3.o delta.o diskio.o daemonize.o -o ddsnap -lpopt -lz

devspam: tests/devspam.c trace.h
	$(CC) $< $(CFLAGS) $(CPPFLAGS) -o $@

ddsnap-sb: ddsnap-sb.c diskio.o buffer.o $(deps)
	$(CC) ddsnap-sb.c $(CFLAGS) $(CPPFLAGS) buffer.o diskio.o -o $@

ddsnap.8.gz: ./man/ddsnap.8
	gzip -c --best ./man/ddsnap.8 > ddsnap.8.gz
nblock_write.8.gz: ./man/nblock_write.8
	gzip -c --best ./man/nblock_write.8 > nblock_write.8.gz
ddsnap-sb.8.gz: ./man/ddsnap-sb.8
	gzip -c --best ./man/ddsnap-sb.8 > ddsnap-sb.8.gz

build.h: $(ddsnap_agent_deps) $(ddsnapd_deps) $(ddsnap_deps) 
	rm -f $@
	@echo '/*' > $@
	@echo ' * This file is generated automatically by the Makefile.' >> $@
	@echo ' */' >> $@
	@echo '' >> $@
	@echo '#define VERSION_STRING "'`cat VERSION_STRING || echo unknown`'"' >> $@
	@echo '#define BUILD_DATE "'`date`'"' >> $@
	@echo '#define BUILD_USER "'`whoami`'"' >> $@
	@echo '#define BUILD_HOST "'`hostname`'"' >> $@


# Just tests from here on.
device=test
port=8080

# Use an anonymous socket (@ is ddsnap's anon socket name extension)
sock=@test

# Intended to run as non-root user (recommended)
# These devices need a+rw permission:
meta=hda5

data0=hda6
#data0=nbd0

data1=hda7
#data1=mapper/loop0

data0=hda6
data1=hda7
data2=hda8
data3=hda9
data4=hda10

data=/dev/$(data0) /dev/$(data1) /dev/$(data2) /dev/$(data3) /dev/$(data4)
#data=/dev/$(data0) /dev/$(data1)

# README test values
origindev=/dev/hdb1
snapstoredev=/dev/hdb2

test-init:
	killall ddsnap-agent || true
	killall ddsnap-server || true
#	/sbin/dmsetup remove_all || true
	dd if=/dev/zero of=test-snapstore bs=1M count=10 2>/dev/null
	dd if=/dev/zero of=test-origin bs=1M count=10 2>/dev/null
#	insmod /src/2.6.17-rc6-dd/drivers/md/dm-ddsnap.ko || true
	./ddsnap initialize test-snapstore test-origin

test-agent:
	./ddsnap agent /tmp/control --logfile /tmp/agent-log

test-server:
	./ddsnap server test-snapstore test-origin /tmp/control /tmp/server --logfile /tmp/agent-log --foreground

test-create:
	./ddsnap create /tmp/server 0
 
test-delete:
	./ddsnap delete /tmp/server 0

test-create-origin:
	echo 0 $$(./ddsnap status /tmp/server --size) ddsnap test-snapstore test-origin /tmp/control -1 | sudo /sbin/dmsetup create snapshot
.PHONY: origin

snap0:
	sudo ./ddsnap create /tmp/server 0
	echo 0 $$(./ddsnap status /tmp/server --size | tail -n 1) ddsnap /dev/test-snapstore /dev/test-origin /tmp/control 0 | sudo /sbin/dmsetup create snapshot0
.PHONY: snap0

snap1:
	sudo ./ddsnap create /tmp/server 1
	echo 0 $$(./ddsnap status /tmp/server --size | tail -n 1) ddsnap /dev/test-snapstore /dev/test-origin /tmp/control 1 | sudo /sbin/dmsetup create snapshot1
.PHONY: snap1

snap2:
	sudo ./ddsnap create /tmp/server 2
	echo 0 $$(./ddsnap status /tmp/server --size | tail -n 1) ddsnap /dev/test-snapstore /dev/test-origin /tmp/control 2 | sudo /sbin/dmsetup create snapshot2
.PHONY: snap2

snap3:
	sudo ./ddsnap create /tmp/server 3
	echo 0 $$(./ddsnap status /tmp/server --size | tail -n 1) ddsnap /dev/test-snapstore /dev/test-origin /tmp/control 3 | sudo /sbin/dmsetup create snapshot3
.PHONY: snap3
 
list:
	sudo ./ddsnap list /tmp/server
.PHONY: list


changelist:	changelist0-1
.PHONY: changelist

changelist0-1:
	sudo ./ddsnap delta changelist /tmp/server $@ 0 1

changelist0-2:
	sudo ./ddsnap delta changelist /tmp/server $@ 0 2

changelist1-2:
	sudo ./ddsnap delta changelist /tmp/server $@ 1 2


delta:	deltafile0-1
.PHONY: delta

deltafile0-1: changelist0-1
	sudo ./ddsnap delta create -r changelist0-1 $@ /dev/mapper/snapshot

deltafile0-2: changelist0-2
	sudo ./ddsnap delta create -r changelist0-2 $@ /dev/mapper/snapshot

deltafile1-2: changelist1-2
	sudo ./ddsnap delta create -r changelist1-2 $@ /dev/mapper/snapshot


ddsnap-test3: 
	sudo ./devspam /dev/mapper/torigin write 1000 12

ddsnap-test9:
	sudo /sbin/dmsetup remove torigin
