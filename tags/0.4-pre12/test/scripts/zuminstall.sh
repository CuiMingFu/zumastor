#!/bin/sh

#set -x

#
# Potentially modifiable parameters.
#
# Working directory, where images and ancillary files live.
WORKDIR=~/local
# Where the svn output goes.
BUILDLOG=build.log
# Name of target Zumastor support directory.  This must match the name in the
# preseed file!  Note:  No leading "/"!
ZUMADIR=zinst
# Where we put the build.
BUILDDIR=`pwd`/build
# Default config file.
CONFIG=${BUILDDIR}/zumastor/test/config/qemu-config

PACKAGEDIR=""

usage()
{
	echo "Usage: $0 [-p <path>] [-c <config file>] <hostname/IP address>" >&2
	echo "Where <path> is the directory that contains the Zumastor .deb files" >&2
	echo "and <config file> is a kernel config file to be used for the build." >&2
	exit 1
}

update_build()
{
	CURDIR=`pwd`
	mkdir -p ${BUILDDIR}
	cd ${BUILDDIR}
	echo -ne Getting zumastor sources from subversion ...
	if [ -e zumastor -a -f zumastor/build_packages.sh ]; then
		svn update zumastor >> $BUILDLOG 2>&1 || exit $?
	else
		svn checkout http://zumastor.googlecode.com/svn/trunk/ zumastor >> $BUILDLOG 2>&1 || exit $?
	fi
	if [ ! -f zumastor/build_packages.sh ]; then
		echo "No build_packages script found!" >&2
		usage
	fi
	if [ ! -f ${CONFIG} ]; then
		echo "No kernel config file \"${CONFIG}\" found!" >&2
		exit 1
	fi
	cd ${CURDIR}
	sh ${BUILDDIR}/zumastor/build_packages.sh ${CONFIG} >>${BUILDLOG} 2>&1
}

while getopts "c:p:" option ; do
	case "$option" in
	c)	CONFIG="$OPTARG";;
	p)	PACKAGEDIR="$OPTARG";;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
if [ $# -lt 1 ]; then
	usage
fi
#
# If the package directory doesn't start with a "/" then it's relative to
# the current directory.
#
if [ "${PACKAGEDIR}" != "" -a "${PACKAGEDIR:0:1}" != "/" ]; then
	PACKAGEDIR=`pwd`/${PACKAGEDIR}
fi
#
# If the package directory doesn't start with a "/" then it's relative to
# the current directory.
#
if [ "${CONFIG}" != "" -a "${CONFIG:0:1}" != "/" ]; then
	CONFIG=`pwd`/${CONFIG}
fi
#
# If we didn't get a package directory on the command line in which to look
# for the zumastor .deb files, just check out the latest source, build it
# and use the .deb files so produced.
#
if [ "${PACKAGEDIR}" = "" ]; then
	#
	# Verify that the config file actually exists.
	#
	if [ ! -f "${CONFIG}" ]; then
		echo "Config file ${CONFIG} doesn't exist!" >&2
		exit 1
	fi
	echo "No package directory, building new packages in ${BUILDDIR} with config file ${CONFIG}." >&2
	update_build
	PACKAGEDIR=${BUILDDIR}
else
	#
	# Verify that the package directory actually exists.
	#
	if [ ! -d "$PACKAGEDIR" ]; then
		echo "Package dir ${PACKAGEDIR} doesn't exist!" >&2
		exit 1
	fi
	echo "Using package directory ${PACKAGEDIR}." >&2
fi
#
# Find the Zumastor packages.  We want the latest, therefore the last in the
# alphabetically-sorted list.
#
cd $PACKAGEDIR
KHDR=`ls kernel-headers-*.deb | tail -1`
KIMG=`ls kernel-image-*.deb | tail -1`
DDSN=`ls ddsnap*.deb | tail -1`
ZUMA=`ls zumastor*.deb | tail -1`
fail=0
if [ ! -f "${KHDR}" ]; then
	echo "No kernel-headers package found!" >&2
	fail=1
fi
if [ ! -f "${KIMG}" ]; then
	echo "No kernel-image package found!" >&2
	fail=1
fi
if [ ! -f "${DDSN}" ]; then
	echo "No ddsnap package found!" >&2
	fail=1
fi
if [ ! -f "${ZUMA}" ]; then
	echo "No zumastor package found!" >&2
	fail=1
fi
PACKAGES="${PACKAGEDIR}/${KHDR} ${PACKAGEDIR}/${KIMG} ${PACKAGEDIR}/${DDSN} ${PACKAGEDIR}/${ZUMA}"
cd ${WORKDIR}
#
# Create the zumastor install script.
#
cat <<EOF_zinstall.sh >zinstall.sh
#!/bin/sh

#
# Install dependencies, including test dependencies.
#
/usr/bin/apt-get -y install tree
/usr/bin/apt-get -y install dmsetup
/usr/bin/apt-get -y install openssh-server
/usr/bin/apt-get -y install make
/usr/bin/apt-get -y install gcc
/usr/bin/apt-get -y install libc6-dev

cd /${ZUMADIR}
#
# Install the packages that have already been placed here.
#
dpkg -i ${KHDR}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${KHDR} failed: $?!" >&2
	exit 1
fi
dpkg -i ${KIMG}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${KIMG} failed: $?!" >&2
	exit 1
fi
dpkg -i ${DDSN}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${DDSN} failed: $?!" >&2
	exit 1
fi
dpkg -i ${ZUMA}
if [ $? -ne 0 ]; then
	echo "dpkg -i ${ZUMA} failed: $?!" >&2
	exit 1
fi
#
# Kernels running under qemu need 'noapic.'  Figure out whether we need it
# and, if so, add it to our kernel line.
#
grep "^kernel.*noapic" /boot/grub/menu.lst
if [ $? -eq 0 ]; then
	sed --in-place '/^kernel.*zumastor/s/$/ noapic/' /boot/grub/menu.lst
fi
#
# Set up ssh if necessary.
#
mkdir -p /root/.ssh
if [ ! -f /root/.ssh/id_rsa.pub ]; then
	ssh-keygen -t rsa -f /root/.ssh/id_rsa -N "" -C root@`hostname`
fi
#
# Everything is installed, reboot the system into the zumastor kernel.
#
shutdown -r now &
exit 0
EOF_zinstall.sh
chmod 755 zinstall.sh

for TARGET in $@
do
	#
	# Attempt to copy our public key into the authorized_keys file on the
	# target.  We skip the target if this fails.
	#
	ssh -o StrictHostKeyChecking=no root@${TARGET} "cat >>~/.ssh/authorized_keys" <~/.ssh/id_rsa.pub
	if [ $? -ne 0 ]; then
		echo "\"ssh root@${TARGET}\" failed!" >&2
		continue
	fi
	#
	# Now make the working directory.  Hopefully we won't have any more
	# password or passphrase prompts.
	#
	ssh -o StrictHostKeyChecking=no root@${TARGET} "/bin/mkdir -p /${ZUMADIR}"
	if [ $? -ne 0 ]; then
		echo "ssh root@${TARGET} (mkdir) failed!" >&2
		continue
	fi
	#
	# Copy the packages and install script to the target.
	#
	scp -l 10240 -C ${PACKAGES} zinstall.sh root@${TARGET}:/${ZUMADIR}
	if [ $? -ne 0 ]; then
		echo "scp root@${TARGET} failed!" >&2
		continue
	fi
	#
	# Run the install script.  This will install the packages, generate
	# ssh keys if necessary and reboot the system into the Zumastor
	# kernel.
	#
	ssh -o StrictHostKeyChecking=no root@${TARGET} "cd /${ZUMADIR}; ./zinstall.sh"
	if [ $? -ne 0 ]; then
		echo "ssh root@${TARGET} (zinstall) failed!" >&2
		continue
	fi
done

exit 0
