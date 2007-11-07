#!/bin/sh

#set -x

# User to ssh as on the source and target systems.
SSHUSER=root
# Where the test support scripts are located, so we can find volcreate,
# zinstalltet and zruntet.
SCRIPTS=~/zumastor/test/scripts

usage()
{
	echo "Usage: $0 [-s <script directory>] [-v <volume list>] <source> <target>" >&2
	echo "Where <script directory> is the directory that contains support scripts," >&2
	echo "<volume list> is a space-delimited list of volumes that should be replicated," >&2
	echo "<source> is the system containing the volume(s) to be replicated, and" >&2
	echo "<target> is the system upon which to replicate the volume(s)." >&2
	exit 1
}

canonicalize_hosts()
{
	src=$1
	targ=$2
	# Figure out what each host calls itself.
	sname="`ssh -o StrictHostKeyChecking=no ${SSHUSER}@${src} '/bin/uname -n'`"
	if [ $? -ne 0 ]; then
		echo "ssh ${SSHUSER}@${src} failed!"
		exit 5
	fi
	tname="`ssh -o StrictHostKeyChecking=no ${SSHUSER}@${targ} '/bin/uname -n'`"
	if [ $? -ne 0 ]; then
		echo "ssh ${SSHUSER}@${targ} failed!"
		exit 5
	fi
	# Make sure each can ping the other.
	targping=`ssh -o StrictHostKeyChecking=no ${SSHUSER}@${src} "/bin/ping -c 5 $targ" | head -1`
	if [ $? -ne 0 ]; then
		echo "$src can't ping $targ!" >&2
		exit 5
	fi
	srcping=`ssh -o StrictHostKeyChecking=no ${SSHUSER}@${targ} "/bin/ping -c 5 $src" | head -1`
	if [ $? -ne 0 ]; then
		echo "$targ can't ping $src?!?" >&2
		exit 5
	fi
	targip=`echo $targping | sed -e 's/^.* (\([0-9.][0-9.]*\)) .*(.*$/\1/'`
	srcip=`echo $srcping | sed -e 's/^.* (\([0-9.][0-9.]*\)) .*(.*$/\1/'`
	# If the names aren't already in /etc/hosts, add them.
	ssh -o StrictHostKeyChecking=no ${SSHUSER}@${src} "/bin/grep $tname /etc/hosts" >/dev/null
	if [ $? -ne 0 ]; then
		echo "${targip}	${tname}" | ssh -o StrictHostKeyChecking=no ${SSHUSER}@${src} "cat >>/etc/hosts"
	fi
	ssh -o StrictHostKeyChecking=no ${SSHUSER}@${targ} "/bin/grep $sname /etc/hosts" >/dev/null
	if [ $? -ne 0 ]; then
		echo "${srcip}	${sname}" | ssh -o StrictHostKeyChecking=no ${SSHUSER}@${targ} "cat >>/etc/hosts"
	fi
	echo "$sname $tname"
}

VOLLIST=
while getopts "s:v:" option ; do
	case "$option" in
	s)	SCRIPTS="$OPTARG";;
	v)	VOLLIST="$OPTARG";;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
if [ $# -lt 2 ]; then
	usage
fi
SOURCE=$1
TARGET=$2
#
# Attempt to copy our public key into the authorized_keys file on the
# source and target.  We abort if this fails.
#
ssh -o StrictHostKeyChecking=no ${SSHUSER}@${SOURCE} "cat >>~/.ssh/authorized_keys" <~/.ssh/id_rsa.pub
if [ $? -ne 0 ]; then
	echo "\"ssh ${SSHUSER}@${SOURCE}\" failed!"
	exit 5
fi
ssh -o StrictHostKeyChecking=no ${SSHUSER}@${TARGET} "cat >>~/.ssh/authorized_keys" <~/.ssh/id_rsa.pub
if [ $? -ne 0 ]; then
	echo "\"ssh ${SSHUSER}@${TARGET}\" failed!"
	exit 5
fi
#
# Canonicalize the hostnames and make sure the hosts agree with each other.
#
set `canonicalize_hosts $SOURCE $TARGET`
SNAME=$1
TNAME=$2
#
# Get the volumes defined on the source machine.
#
srcvols="`ssh -o StrictHostKeyChecking=no ${SSHUSER}@${SOURCE} 'ls /var/lib/zumastor/volumes'`"
if [ $? -ne 0 ]; then
	echo "ssh ${SSHUSER}@${SOURCE} failed!"
	exit 5
fi
#
# Get the volumes defined on the target machine.
#
targvols="`ssh -o StrictHostKeyChecking=no ${SSHUSER}@${TARGET} 'ls /var/lib/zumastor/volumes'`"
if [ $? -ne 0 ]; then
	echo "ssh ${SSHUSER}@${TARGET} failed!"
	exit 5
fi
#
# Make sure the systems each have the others key.  They both have keys if they
# were installed with zuminstall.sh.
#
ssh -o StrictHostKeyChecking=no ${SSHUSER}@${SOURCE} "cat ~/.ssh/id_rsa.pub" | ssh ${SSHUSER}@${TARGET} "cat >>~/.ssh/authorized_keys"
ssh -o StrictHostKeyChecking=no ${SSHUSER}@${TARGET} "cat ~/.ssh/id_rsa.pub" | ssh ${SSHUSER}@${SOURCE} "cat >>~/.ssh/authorized_keys"
#
# Also make sure they have the host keys, just to avoid potential problems.
#
ssh -o StrictHostKeyChecking=no ${SSHUSER}@${SOURCE} "ssh -o StrictHostKeyChecking=no ${SSHUSER}@${TNAME} /bin/true"
ssh -o StrictHostKeyChecking=no ${SSHUSER}@${TARGET} "ssh -o StrictHostKeyChecking=no ${SSHUSER}@${SNAME} /bin/true"
#
# Now replicate the volumes.
#
for vol in ${srcvols}; do
	#
	# If the volume is already replicated to the target, skip it.
	#
	ssh ${SSHUSER}@${SOURCE} "/usr/bin/test -d /var/lib/zumastor/volumes/${vol}/${TARGET}"
	if [ $? -eq 0 ]; then
		echo "Volume ${vol} already replicated between ${SOURCE} and ${TARGET}." >&2
		continue
	fi
	#
	# If the target already has a volume with this name, we can do nothing.
	#
	ssh root@${HOST} "/usr/bin/test -b /dev/mapper/${vol}"
	if [ $? -eq 0 ]; then
		echo "Volume ${vol} already exists on ${TARGET}." >&2
		continue
	fi
	origdev=`ssh ${SSHUSER}@${SOURCE} "readlink /var/lib/zumastor/volumes/${vol}/device/origin"`
	snapdev=`ssh ${SSHUSER}@${SOURCE} "readlink /var/lib/zumastor/volumes/${vol}/device/snapstore"`
	origsize=`ssh ${SSHUSER}@${SOURCE} "fdisk -l ${origdev}" | grep "^Disk .*bytes" | cut -f3 -d\ `
	snapsize=`ssh ${SSHUSER}@${SOURCE} "fdisk -l ${snapdev}" | grep "^Disk .*bytes" | cut -f3 -d\ `
	volsize=`expr ${origsize} + ${snapsize}`
	scp ${SCRIPTS}/volcreate.sh ${SSHUSER}@${TARGET}:/tmp
	voldevs=`ssh ${SSHUSER}@${TARGET} "sh /tmp/volcreate.sh -O ${origsize} -S ${snapsize}"`
	if [ $? -ne 0 ]; then
		echo "$0:  Can't create replication volumes."
		exit 5
	fi
	set ${voldevs}
	origdev=$1
	snapdev=$2
	ssh ${SSHUSER}@${TARGET} "/bin/zumastor define volume ${vol} ${origdev} ${snapdev} --initialize"
	ssh ${SSHUSER}@${TARGET} "/bin/zumastor define source ${vol} ${SNAME} -p 600"
	ssh ${SSHUSER}@${SOURCE} "/bin/zumastor define target ${vol} ${TNAME}:11235 30"
	ssh ${SSHUSER}@${TARGET} "/bin/zumastor start source ${vol}"
done
