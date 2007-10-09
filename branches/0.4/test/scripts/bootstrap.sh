#!/bin/sh

set -x

#
# Potentially modifiable parameters.
#
# Working directory, where images and ancillary files live.
WORKDIR=~/local
# Where the svn output goes.
SVNLOG=svn.log
# Name of the main installed Qemu disk image.
TESTIMG=${WORKDIR}/test.img
# Names of the working replication source and target disk images.
SOURCEIMG=${WORKDIR}/source.img
TARGETIMG=${WORKDIR}/target.img
# Names of the test origin and snapshop store disk images for the source
# and target.
ZUMSRCIMG=${WORKDIR}/zumsource.img
ZUMTGTIMG=${WORKDIR}/zumtarget.img
# Name of target Zumastor support directory.  This must match the name in the
# preseed file!  Note:  No leading "/"!
ZUMADIR=zinst
# Where we put the "zumtest" link so it'll be run at boot.
ZRUNDIR=/etc/rc2.d
# Name of the zumastor volume we create for testing.
TESTVOL=testvol
# Name of the directory upon which we mount that volume.
ZTESTFS=ztestfs
# IP /24 network of test setup.
ZNETWORK=192.168.0
# Name/IP address of replication source virtual machine.
ZSOURCENM=source
ZSOURCEIP=${ZNETWORK}.1
# Name/IP address of replication target virtual machine.
ZTARGETNM=target
ZTARGETIP=${ZNETWORK}.2
# The command that actually runs the tests.
RUNTESTS=${ZUMADIR}/runtests.sh
# Where the tests are actually located.
TESTDIR=tests

usage()
{
	echo "Usage: $0 <path>" 1>&2
	echo "Where <path> is the directory that contains the Zumastor .deb files."
}

update_build()
{
	mkdir -p build
	cd build
	echo -ne Getting zumastor sources from subversion ...
	if [ -e zumastor -a -f zumastor/build_packages.sh ]; then
		svn update zumastor >> $SVNLOG || exit $?
	else
		svn checkout http://zumastor.googlecode.com/svn/trunk/ zumastor >> $SVNLOG || exit $?
	fi
	if [ ! -f zumastor/build_packages.sh ]; then
		usage
		echo "No build_packages script found!"
		exit 1
	fi
	if [ ! -f zumastor/test/config/qemu-config ]; then
		echo "No qemu kernel config file found!"
		exit 1
	fi
	cd ..
	sh build/zumastor/build_packages.sh `pwd`/build/zumastor/test/config/qemu-config
}

#
# If we didn't get a directory on the command line in which to look for the
# zumastor .deb files, just check out the latest source, build it and use
# the .deb files so produced.
#
if [ $# -ne 1 ]; then
	update_build
	PACKAGEDIR=`pwd`/build
else
	PACKAGEDIR=$1
	#
	# Verify that the package directory actually exists.
	#
	if [ ! -d "$PACKAGEDIR" ]; then
		echo "Package dir ${PACKAGEDIR} doesn't exist!"
		exit 1
	fi
fi
#
# Extract the installation ISO from the script
#
instiso=${WORKDIR}/install-$$.iso
trap "rm -f ${instiso}; exit 1" 1 2 3 9
ISOSTART=`grep -n -m 1 "^ISO-START" $0 | cut -f1 -d:`
if [ ! -z "$ISOSTART" ]; then
	ISOSTART=$(expr $ISOSTART + 1)
	tail +${ISOSTART} $0 >${instiso}
else
	# For testing only
	instiso=~/cd/test.iso
fi
#
# Find the Zumastor packages.
#
cd $PACKAGEDIR
KHDR=`ls kernel-headers-*.deb | tail -1`
KIMG=`ls kernel-image-*.deb | tail -1`
DDSN=`ls ddsnap*.deb | tail -1`
ZUMA=`ls zumastor*.deb | tail -1`
fail=0
if [ ! -f "${KHDR}" ]; then
	echo "No kernel-headers package found!"
	fail=1
fi
if [ ! -f "${KIMG}" ]; then
	echo "No kernel-image package found!"
	fail=1
fi
if [ ! -f "${DDSN}" ]; then
	echo "No ddsnap package found!"
	fail=1
fi
if [ ! -f "${ZUMA}" ]; then
	echo "No zumastor package found!"
	fail=1
fi
debtar=${WORKDIR}/debtar-$$.tar
cd ${WORKDIR}
#
# Create the zumastor install script.
#
cat <<EOF_zinstall.sh >zinstall.sh
#!/bin/sh

cd /${ZUMADIR}
#
# Install the packages that have already been placed here by the bootstrap
# script and the preseed mechanism.
#
dpkg -i ${KHDR}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${KHDR} failed: $?!"
	exit 1
fi
dpkg -i ${KIMG}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${KIMG} failed: $?!"
	exit 1
fi
dpkg -i ${DDSN}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${DDSN} failed: $?!"
	exit 1
fi
dpkg -i ${ZUMA}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${ZUMA} failed: $?!"
	exit 1
fi
#
# Set 'noapic' on the zumastor kernel since qemu doesn't have one.
#
sed --in-place '/^kernel.*zumastor/s/$/ noapic/' /boot/grub/menu.lst
#
# Install our test startup script that will run when we reboot.
#
cp zstartup.sh /etc/init.d/zstartup
chmod 755 /etc/init.d/zstartup
ln -s ../init.d/zstartup ${ZRUNDIR}/S99zstartup
#
# Install 'tree' dependency as well as openssh-server
#
/usr/bin/apt-get -y install tree
/usr/bin/apt-get -y install openssh-server
#
# Install contents of CD zumastor directory, if any.
#
if [ -d /cdrom/zumastor ]; then
	cp -r /cdrom/zumastor /zuma
fi
#
# Stuff our IP/name definitions into /etc/hosts.
#
echo "${ZSOURCEIP}	${ZSOURCENM}" >>/etc/hosts
echo "${ZTARGETIP}	${ZTARGETNM}" >>/etc/hosts

#
# Set up ssh
#
mkdir /root/.ssh
cat sourcekey.pub targetkey.pub >/root/.ssh/authorized_keys

exit 0
EOF_zinstall.sh
chmod 755 zinstall.sh
#
# Create the test startup script.  This script runs at boot, sucks in the
# zumastor test configuration script and runs it.
#
cat <<EOF_zstartup >zstartup.sh
#!/bin/sh

echo "Zumastor automated test lashup..."

cd /${ZUMADIR}

#
# Pull in the test configuration script, modefile and any tests that might
# be included.
#
tar xf /dev/hdd
if [ ! -f zumcfg.sh ]; then
	echo "******* MISSING zumcfg.sh FILE! *******"
	exit 2
fi
if [ ! -f zmode.sh ]; then
	echo "******* MISSING zmode.sh FILE! *******"
	exit 2
fi
. zmode.sh
grep -q eth1 /etc/network/interfaces
if [ \$? -ne 0 ]; then
	#
	# Configure our network appropriately.
	#
	echo "auto eth1" >>/etc/network/interfaces
	echo "iface eth1 inet static" >>/etc/network/interfaces
	if [ "zmodevar" = "source" ]; then
		echo "	address ${ZSOURCEIP}" >>/etc/network/interfaces
		hostname ${ZSOURCENM}
		echo ${ZSOURCENM} >/etc/hostname
		cp sourcekey /root/.ssh/id_rsa
		cp sourcekey.pub /root/.ssh/id_rsa.pub
		cp targetkey.pub /root/.ssh/authorized_keys
	else
		echo "	address ${ZTARGETIP}" >>/etc/network/interfaces
		hostname ${ZTARGETNM}
		echo ${ZTARGETNM} >/etc/hostname
		cp targetkey /root/.ssh/id_rsa
		cp targetkey.pub /root/.ssh/id_rsa.pub
		cp sourcekey.pub /root/.ssh/authorized_keys
	fi
	echo "	netmask 255.255.255.0" >>/etc/network/interfaces
	ifdown eth1 >/dev/null 2>&1
	ifup eth1
	zumastor status | grep -q "^Status: running"
	if [ \$? -eq 0 ]; then
		/etc/init.d/zumastor stop
		sleep 5
		/etc/init.d/zumastor start
	fi
fi
route add -net ${ZNETWORK}.0/24 dev eth1

nohup ./zumcfg.sh >/dev/null 2>&1 &
exit 0
EOF_zstartup
sed --in-place 's/zmodevar/\$\{ZMODE\}/' zstartup.sh
chmod 755 zstartup.sh
#
# If we've already built test.img, don't do it again.
#
if [ ! -f test.img ]; then
	#
	# Tar the zumastor install packages.
	#
	cd $PACKAGEDIR
	trap "rm -f ${instiso} ${debtar} test.img; exit 1" 1 2 3 9
	tar cf ${debtar} ${KHDR} ${KIMG} ${DDSN} ${ZUMA}
	cd ${WORKDIR}
	#
	# Generate ssh keys for the source and target, if necessary.
	#
	if [ ! -f sourcekey ]; then
		ssh-keygen -t rsa -f sourcekey -N "" -C root@source
		ssh-keygen -t rsa -f targetkey -N "" -C root@target
	fi
	#
	# Append scripts and keys to the tarfile.
	#
	tar -r -f ${debtar} zinstall.sh zstartup.sh sourcekey* targetkey*
	#
	# Create the qemu disk image then boot qemu from our iso to do the
	# install.
	#
	qemu-img create -f qcow2 test.img 10G
	qemu -no-reboot -hdb ${debtar} -cdrom ${instiso} -boot d ${TESTIMG}
	
	rm ${debtar}
fi

trap - 1 2 3 9

#
# Create the zumastor test configuration script.  This script gets run by
# the test startup script at boot and sets up the test environment.
#
cat <<EOF_zumcfg.sh >zumcfg.sh
#!/bin/sh

#
# Source the source/target mode file so we know how we're running.
#
. ./zmode.sh

echo "	Configuring mode zmodevar"

if [ ! -b /dev/mapper/${TESTVOL}1 ]; then
	#
	# Set up our ssh keys.
	#
	if [ "zmodevar" = "source" ]; then
		#
		# Rendezvous with the target virtual machine.  Wait at most
		# ten minutes for two ping replies.
		#
		ping -c 2 -w 600 ${ZTARGETNM}
#		while [ \$? -eq 1 ]; do
#			ping -c 2 -w 600 ${ZTARGETNM}
#		done
		ssh-keyscan -t rsa target >/root/.ssh/known_hosts 2>/zuma/zt.log
	else
		#
		# Rendezvous with the source virtual machine.  Wait at most
		# ten minutes for two ping replies.
		#
		ping -c 2 -w 600 ${ZSOURCENM}
#		while [ \$? -eq 1 ]; do
#			ping -c 2 -w 600 ${ZSOURCENM}
#		done
		ssh-keyscan -t rsa source >/root/.ssh/known_hosts 2>/zuma/zt.log
	fi
	#
	# Create the volumes
	#
	parted -s /dev/hdb mklabel msdos >>/zuma/zt.log 2>&1
	parted -s /dev/hdb mkpart primary 0 32768 >>/zuma/zt.log 2>&1
	parted -s /dev/hdb check 1 >>/zuma/zt.log 2>&1
	sleep 10
	pvcreate /dev/hdb1 >>/zuma/zt.log 2>&1
	vgcreate sysvg /dev/hdb1 >>/zuma/zt.log 2>&1
	lvcreate --size 2g -n origstore1 sysvg >>/zuma/zt.log 2>&1
	lvcreate --size 8g -n snapstore1 sysvg >>/zuma/zt.log 2>&1
	lvcreate --size 2g -n origstore2 sysvg >>/zuma/zt.log 2>&1
	lvcreate --size 8g -n snapstore2 sysvg >>/zuma/zt.log 2>&1
	lvcreate --size 2g -n origstore3 sysvg >>/zuma/zt.log 2>&1
	lvcreate --size 8g -n snapstore3 sysvg >>/zuma/zt.log 2>&1
	echo -n "	Creating ${TESTVOL}1..."
	zumastor define volume ${TESTVOL}1 /dev/sysvg/origstore1 /dev/sysvg/snapstore1 --initialize >>/zuma/zt.log 2>&1
	echo -n "${TESTVOL}2..."
	zumastor define volume ${TESTVOL}2 /dev/sysvg/origstore2 /dev/sysvg/snapstore2 --initialize >>/zuma/zt.log 2>&1
	echo -n "${TESTVOL}3..."
	zumastor define volume ${TESTVOL}3 /dev/sysvg/origstore3 /dev/sysvg/snapstore3 --initialize >>/zuma/zt.log 2>&1
	echo "done."
	# Wait for things to calm down a bit.
	sleep 60
	if [ "zmodevar" = "source" ]; then
		echo -n "	Creating file systems..."
		mkfs.ext3 /dev/mapper/${TESTVOL}1 >>/zuma/zt.log 2>&1
		mkfs.ext3 /dev/mapper/${TESTVOL}2 >>/zuma/zt.log 2>&1
		mkfs.ext3 /dev/mapper/${TESTVOL}3 >>/zuma/zt.log 2>&1
		echo "done."
		zumastor define master ${TESTVOL}1 -h 24 -d 7 >>/zuma/zt.log 2>&1
		zumastor define master ${TESTVOL}2 -h 24 -d 7 >>/zuma/zt.log 2>&1
		zumastor define master ${TESTVOL}3 -h 24 -d 7 >>/zuma/zt.log 2>&1
		zumastor define target ${TESTVOL}1 ${ZTARGETNM}:11235 30 >>/zuma/zt.log 2>&1
		zumastor define target ${TESTVOL}2 ${ZTARGETNM}:11236 30 >>/zuma/zt.log 2>&1
		zumastor define target ${TESTVOL}3 ${ZTARGETNM}:11237 30 >>/zuma/zt.log 2>&1
		mkdir -p /${ZTESTFS}1 /${ZTESTFS}2 /${ZTESTFS}3 >>/zuma/zt.log 2>&1
	else
		zumastor define source ${TESTVOL}1 ${ZSOURCENM} 600 >>/zuma/zt.log 2>&1
		zumastor define source ${TESTVOL}2 ${ZSOURCENM} 600 >>/zuma/zt.log 2>&1
		zumastor define source ${TESTVOL}3 ${ZSOURCENM} 600 >>/zuma/zt.log 2>&1
		zumastor start source ${TESTVOL}1 >>/zuma/zt.log 2>&1
		zumastor start source ${TESTVOL}2 >>/zuma/zt.log 2>&1
		zumastor start source ${TESTVOL}3 >>/zuma/zt.log 2>&1
	fi
fi

if [ "zmodevar" = "source" ]; then
	echo "	Mounting ${ZTESTFS}1, ${ZTESTFS}2, ${ZTESTFS}3."
	mount /dev/mapper/${TESTVOL}1 /${ZTESTFS}1 >>/zuma/zt.log 2>&1
	mount /dev/mapper/${TESTVOL}2 /${ZTESTFS}2 >>/zuma/zt.log 2>&1
	mount /dev/mapper/${TESTVOL}3 /${ZTESTFS}3 >>/zuma/zt.log 2>&1
fi

if [ -n "${RUNTESTS}" -a -x ${RUNTESTS} ]; then
	${RUNTESTS} ${TESTDIR} &
fi

exit 0

EOF_zumcfg.sh
sed --in-place 's/zmodevar/\$\{ZMODE\}/' zumcfg.sh
chmod 755 zumcfg.sh

#
# These files are in a tarfile pointed at by the "hdd" virtual device; they
# let the virtual machine know the mode in which it should run as well as
# other parameters.  The extra comment lines are to pad it out to a reasonable
# size for a 'raw' disk image.
#
if [ ! -f source-config.sh ]; then
	cat <<source-config_EOF >source-config.sh
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
ZMODE=source
ZUMADIR=${ZUMADIR}
ZRUNDIR=${ZRUNDIR}
TESTVOL=${TESTVOL}
ZTESTFS=${ZTESTFS}
ZNETWORK=${ZNETWORK}
ZSOURCENM=${ZSOURCENM}
ZSOURCEIP=${ZSOURCEIP}
ZTARGETNM=${ZTARGETNM}
ZTARGETIP=${ZTARGETIP}
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
source-config_EOF
	cat <<target-config_EOF >target-config.sh
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
ZMODE=target
ZUMADIR=${ZUMADIR}
ZRUNDIR=${ZRUNDIR}
TESTVOL=${TESTVOL}
ZTESTFS=${ZTESTFS}
ZNETWORK=${ZNETWORK}
ZSOURCENM=${ZSOURCENM}
ZSOURCEIP=${ZSOURCEIP}
ZTARGETNM=${ZTARGETNM}
ZTARGETIP=${ZTARGETIP}
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
#-------------------------------------------------------------------------------
target-config_EOF
	chmod 444 source-config.sh target-config.sh
fi
#
# Build source and target disk images.
#
qemu-img create -f qcow2 ${ZUMSRCIMG} 32G
qemu-img create -f qcow2 ${ZUMTGTIMG} 32G
# Clone the test disk image for our source instance.
cp ${TESTIMG} ${SOURCEIMG} &
rm -f qemu-source.pid qemu-target.pid
# Prepare the tarfile and start the source instance...
rm -f zmode.sh
cp source-config.sh zmode.sh
tar cf zstart-source.tar zumcfg.sh zmode.sh
if [ -n "${TESTDIR}" -a -d "${TESTDIR}" ]; then
	tar -r -f zstart-source.tar ${TESTDIR}
fi
# Wait for the test disk image copy to complete.
wait
qemu --pidfile qemu-source.pid -no-reboot -serial tcp::4444,server,nowait -m 512 -hdd zstart-source.tar -boot c -net nic,vlan=1,macaddr=00:e0:10:00:00:01 -net socket,vlan=1,listen=:3333 -hdb ${ZUMSRCIMG} ${SOURCEIMG} &
# Clone the test disk image for our target instance.
cp ${TESTIMG} ${TARGETIMG}
# Wait a minute to let the source instance get going.
sleep 60
# Prepare the tarfile and start the target instance.
rm -f zmode.sh
cp target-config.sh zmode.sh
tar cf zstart-target.tar zumcfg.sh zmode.sh
qemu -pidfile qemu-target.pid -no-reboot -serial pty -m 512 -hdd zstart-target.tar -boot c -net nic,vlan=1,macaddr=00:e0:10:00:00:02 -net socket,vlan=1,connect=127.0.0.1:3333 -hdb ${ZUMTGTIMG} ${TARGETIMG} &
rm -f zmode.sh

wait
exit 0

#ISO-START
