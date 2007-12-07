UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

SRCDIR = $(FSTRESS_HOME)/src
TSTDIR = $(FSTRESS_HOME)/src-testprogs
OBJDIR = $(FSTRESS_HOME)/obj-$(UNAME_S)-$(UNAME_M)

CC = gcc
CCFLAGS = -g -Wall -I$(SRCDIR) 
CCFLAGS += -DIPPROTO_TUF=132
VPATH = $(SRCDIR):$(TSTDIR)
LIBS =

# ---------------------------------------------------------------
# hostos specific stuff
# ---------------------------------------------------------------

ifeq "$(UNAME_S)" "FreeBSD"
SUPPORTED_UNAME = yes
USE_GMP = yes
endif
ifeq "$(UNAME_S)" "Linux"
SUPPORTED_UNAME = yes
USE_GMP = no
endif
ifeq "$(UNAME_S)" "SunOS"
SUPPORTED_UNAME = yes
USE_GMP = no
LIBS += -lnsl -lsocket
endif

# ---------------------------------------------------------------
# make sure this is a supported hostos
# ---------------------------------------------------------------

ifneq "$(SUPPORTED_UNAME)" "yes"
error:
	echo "unsupported OS $(UNAME_S)"
endif

# ---------------------------------------------------------------
# setup for the correct math library
# ---------------------------------------------------------------

ifeq "$(USE_GMP)" "yes"
CCFLAGS += -DGMP
LIBS += -lgmp
else
LIBS += -lm
endif

# ---------------------------------------------------------------
# generic rules
# ---------------------------------------------------------------

firstrule:
	mkdir -p $(OBJDIR)
	cd $(OBJDIR) ; $(MAKE) -f $(FSTRESS_HOME)/Makefile all

all: dns-test mount-test readdir-test metronome-test distheap-test \
	nameset-test operation-test createtree-test measure_op-test \
	fstress_init fstress_fill fstress_run gen_dist

clean:
	-/bin/rm obj-*-*/*.o *~ src/*~ src-testprogs/*~ bin/*~

clobber: clean
	-/bin/rm -rf obj-*-*

.c.o:
	$(CC) $(CCFLAGS) -c $<

# ---------------------------------------------------------------
# specific rules
# ---------------------------------------------------------------

dns-test-O = dns-test.o dns.o report.o
dns-test: $(dns-test-O)
	$(CC) $(CCFLAGS) -o $@ $(dns-test-O) $(LIBS)

mount-test-O = mount-test.o mount.o dns.o msg.o my_malloc.o rpc.o report.o
mount-test: $(mount-test-O)
	$(CC) $(CCFLAGS) -o $@ $(mount-test-O) $(LIBS)

readdir-test-O = readdir-test.o nfs.o mount.o dns.o msg.o my_malloc.o \
	rpc.o report.o
readdir-test: $(readdir-test-O)
	$(CC) $(CCFLAGS) -o $@ $(readdir-test-O) $(LIBS)

metronome-test-O = metronome-test.o metronome.o dns.o msg.o my_malloc.o \
	rpc.o nfs.o timer.o report.o
metronome-test: $(metronome-test-O)
	$(CC) $(CCFLAGS) -o $@ $(metronome-test-O) $(LIBS)

distheap-test-O = distheap-test.o distheap.o report.o
distheap-test: $(distheap-test-O)
	$(CC) $(CCFLAGS) -o $@ $(distheap-test-O) $(LIBS)

distheap-gen-O = distheap-gen.o distheap.o report.o
distheap-gen: $(distheap-gen-O)
	$(CC) $(CCFLAGS) -o $@ $(distheap-gen-O) $(LIBS)

nameset-test-O = nameset-test.o nameset.o distheap.o report.o
nameset-test: $(nameset-test-O)
	$(CC) $(CCFLAGS) -o $@ $(nameset-test-O) $(LIBS)

operation-test-O = operation-test.o operation.o metronome.o timer.o dns.o \
	msg.o my_malloc.o rpc.o nfs.o report.o measure_op.o nameset.o \
	distheap.o mount.o linger.o distribution.o
operation-test: $(operation-test-O)
	$(CC) $(CCFLAGS) -o $@ $(operation-test-O) $(LIBS)

createtree-test-O = createtree-test.o createtree.o operation.o metronome.o \
	timer.o mount.o dns.o msg.o my_malloc.o rpc.o nfs.o report.o \
	distheap.o nameset.o distribution.o measure_op.o linger.o
createtree-test: $(createtree-test-O)
	$(CC) $(CCFLAGS) -o $@ $(createtree-test-O) $(LIBS)

measure_op-test-O = measure_op-test.o measure_op.o timer.o report.o \
	nfs.o msg.o my_malloc.o rpc.o
measure_op-test: $(measure_op-test-O)
	$(CC) $(CCFLAGS) -o $@ $(measure_op-test-O) $(LIBS)

# ---------------------------------------------------------------

fstress_init-O = fstress_init.o nameset.o distheap.o report.o
fstress_init: $(fstress_init-O)
	$(CC) $(CCFLAGS) -o $@ $(fstress_init-O) $(LIBS)

fstress_fill-O = fstress_fill.o createtree.o operation.o metronome.o \
	timer.o mount.o dns.o msg.o my_malloc.o rpc.o nfs.o report.o \
	distheap.o nameset.o distribution.o measure_op.o linger.o
fstress_fill: $(fstress_fill-O)
	$(CC) $(CCFLAGS) -o $@ $(fstress_fill-O) $(LIBS)

fstress_run-O = fstress_run.o operation.o metronome.o \
	timer.o mount.o dns.o msg.o my_malloc.o rpc.o nfs.o report.o \
	distheap.o nameset.o distribution.o gen_op.o measure_op.o linger.o
fstress_run: $(fstress_run-O)
	$(CC) $(CCFLAGS) -o $@ $(fstress_run-O) $(LIBS)

# ---------------------------------------------------------------

gen_dist: gen_dist.o
	$(CC) $(CCFLAGS) -o $@ gen_dist.o -lm

# ---------------------------------------------------------------

sample_workloads:
	csh $(FSTRESS_HOME)/bin/workloads_gen.csh

# ---------------------------------------------------------------

WEBDIR = /usr/project/ari1/www/fstress/download

duke_release:
	find $(FSTRESS_HOME) -type f -name "*~" -exec /bin/rm {} \;
	(cd $(FSTRESS_HOME)/.. ; \
	tar zcf $(WEBDIR)/fstress-export.tgz \
		fstress/COPYRIGHT \
		fstress/Makefile \
		fstress/bin \
		fstress/src \
		fstress/src-testprogs \
	)
	(cd $(FSTRESS_HOME)/.. ; \
	tar zcf $(WEBDIR)/fstress-docs.tgz \
		fstress/COPYRIGHT \
		fstress/docs \
	)

# ---------------------------------------------------------------
# EOF
# ---------------------------------------------------------------
