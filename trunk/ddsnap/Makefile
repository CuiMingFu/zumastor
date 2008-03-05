PREFIX ?=
BINDIR ?=sbin
MANDIR ?=usr/share/man

kernel=./kernel
INSTDIR=$(PREFIX)/$(BINDIR)
INCLUDES +=-I$(kernel)
CPPFLAGS +=-D_FILE_OFFSET_BITS=64 -DDSNAP_MEM_MONITOR=30 $(INCLUDES)
CFLAGS +=-g -Wall -std=gnu99

deps = Makefile trace.h diskio.h buffer.h list.h sock.h event.h
ddsnap_agent_deps = $(deps) ddsnap.agent.h $(kernel)/dm-ddsnap.h daemonize.h
ddsnapd_deps = $(deps) $(kernel)/dm-ddsnap.h daemonize.h ddsnap.h
ddsnap_deps = $(deps) ddsnap.h ddsnap.agent.h $(kernel)/dm-ddsnap.h
ddraid_deps = $(deps) ddraid.h $(kernel)/dm-ddraid.h
testdir =tests

ddraid_binaries = mkddraid ddraidd ddraid-agent
ddsnap_binaries = ddsnap
# ddraid temporarily removed
#binaries =  $(ddraid_binaries) $(ddsnap_binaries) devspam
binaries =  $(ddsnap_binaries) devspam

all: $(binaries) ddsnap.8.gz
.PHONY: all

clean:
	$(MAKE) -C $(testdir) clean
	rm -f build.h $(binaries) *.o xdelta/*.o a.out ddsnap.8.gz patches/*/AUTO.*
.PHONY: clean

install:
	mkdir -p $(INSTDIR) || true
	cp $(binaries) $(INSTDIR)
	gzip -c ./man/ddsnap.8 > $(PREFIX)/$(MANDIR)/man8/ddsnap.8.gz
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

event.o: event.c $(kernel)/dm-ddsnap.h event.h trace.h Makefile

daemonize.o: daemonize.c $(deps)

mkddraid: ddraid.c diskio.o buffer.o $(ddraid_deps)
	$(CC) -DCREATE ddraid.c $(CFLAGS) $(CPPFLAGS) diskio.o buffer.o -o mkddraid

ddraidd: ddraid.c diskio.o buffer.o $(ddraid_deps)
	$(CC) -DSERVER ddraid.c $(CFLAGS) $(CPPFLAGS) diskio.o buffer.o -o ddraidd

ddraid-agent: ddraid.agent.c $(ddraid_deps)
	$(CC) ddraid.agent.c $(CFLAGS) $(CPPFLAGS) -lpthread -o ddraid-agent -lpopt

ddsnap: ddsnap.c buffer.o ddsnapd.o event.o ddsnap.agent.o xdelta/xdelta3.o delta.o diskio.o daemonize.o $(ddsnap_deps) build.h
	$(CC) ddsnap.c $(CFLAGS) $(CPPFLAGS) buffer.o event.o ddsnapd.o ddsnap.agent.o xdelta/xdelta3.o delta.o diskio.o daemonize.o -o ddsnap -lpopt -lz

devspam: tests/devspam.c trace.h
	$(CC) $< $(CFLAGS) $(CPPFLAGS) -o $@

ddsnap.8.gz: ./man/ddsnap.8
	gzip -c ./man/ddsnap.8 > ddsnap.8.gz

build.h: $(ddsnap_agent_deps) $(ddsnapd_deps) $(ddsnap_deps) 
	rm -f $@
	@echo '/*' > $@
	@echo ' * This file is generated automatically by the Makefile.' >> $@
	@echo ' */' >> $@
	@echo '' >> $@
	@echo '#define SVN_VERSION "'`cat SVNREV || svnversion .`'"' >> $@
	@echo '#define BUILD_DATE "'`date`'"' >> $@
	@echo '#define BUILD_USER "'`whoami`'"' >> $@
	@echo '#define BUILD_HOST "'`hostname`'"' >> $@


# Just tests from here on.
device=test
port=8080

# Use an anonymous socket (@ is ddraid's anon socket name extension)
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

ddraid-test: ddraid-test1 ddraid-test2

ddraid-test1:
	killall ddraidd || true
	sudo killall ddraid-agent || true
	./mkddraid /dev/$(meta)
	sudo /sbin/dmsetup remove $(device) || true
	sudo ./ddraid-agent @test
	./ddraidd /dev/$(meta) $(data) $(sock) $(port)

ddraid-test2: ddraid-order2

ddraid-order0:
	echo 0 204800 ddraid 2 $(data) $(sock) | sudo /sbin/dmsetup create $(device)
	sudo chmod a+rw /dev/mapper/$(device)

ddraid-order1:
	echo 0 204800 ddraid 3 $(data) $(sock) | sudo /sbin/dmsetup create $(device)
	sudo chmod a+rw /dev/mapper/$(device)

ddraid-order2:
	echo 0 204800 ddraid 5 $(data) $(sock) | sudo /sbin/dmsetup create $(device)
	sudo chmod a+rw /dev/mapper/$(device)

ddraid-test3:
	./devspam /dev/mapper/$(device) write 100 20 77

ddraid-test4:
	./devspam /dev/mapper/$(device) read 100 20 77

ddraid-test8:
	killall ddraidd

ddraid-test9:
	sudo /sbin/dmsetup remove $(device)

#ddsnap-test: ddsnap-test1 ddsnap-test2 ddsnap-test3

dd-init: 
	ln -s $(snapstoredev) /dev/test-snapstore || true
	ln -s $(origindev) /dev/test-origin || true
#	insmod /src/2.6.17-rc6-dd/drivers/md/dm-ddsnap.ko || true
	killall ddsnap || true
	/sbin/dmsetup remove_all || true
	./ddsnap initialize /dev/test-snapstore /dev/test-origin 

dd-agent:
	./ddsnap agent /tmp/control

dd-server:
	./ddsnap server /dev/test-snapstore /dev/test-origin /tmp/control /tmp/server

dd-create:
	./ddsnap create /tmp/server 0
 
dd-delete:
	./ddsnap delete /tmp/server 0

origin:
	echo 0 $$(./ddsnap status /tmp/server --size | tail -n 1) ddsnap /dev/test-snapstore /dev/test-origin /tmp/control -1 | sudo /sbin/dmsetup create snapshot
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
#	sudo ./devpoke /dev/mapper/torigin write 2
#	sudo ./devpoke /dev/mapper/torigin read 2
#	sleep 1
#	killall ddsnap-server
	sudo ./devspam /dev/mapper/torigin write 19 77

ddsnap-test4:
#	sudo ./devspam /dev/mapper/torigin write 1 77

ddsnap-test9:
	sudo /sbin/dmsetup remove torigin

