#!/bin/sh

#set -x

# Name of the volume group that has the origin and snap store volumes.
VGNAME=sysvg
# Name and size in megabytes of the origin volume.
ORIGNAME=ztestorig
ORIGSIZE=4096
# Name and size in megabytes of the snap store volume.
SNAPNAME=ztestsnap
SNAPSIZE=2048
# Name of the Zumastor volume.
TESTVOL=testvol
# Where the test support scripts are located, so we can find volcreate,
# zinstalltet and zruntet.
SCRIPTS=~/zumastor/test/scripts

REPLHOST=

usage()
{
	echo "Usage: $0 [-s <script directory>] [-t <test directory>]"
	echo "    [-d <TET directory>] [-v <test volume>] <hostname> [<replication hostname>]" 1>&2
	echo "Where"
	echo "	<script directory> is the directory that contains support scripts,"
	echo "	<test directory> is the directory that contains the TET-based tests,"
	echo "	<TET directory> is the directory that contains the TET distribution,"
	echo "	<test volume> is the name of the volume that will be used for testing,"
	echo "	<hostname> identifies a master host upon which to install and run the"
	echo "	tests, and <replication hostname> identifies a host that will be used"
	echo "	as a replication target as the test runs."
	exit 1
}

TESTDIR="TETtests"
TETDIST="TET"
while getopts "d:s:t:v:" option ; do
	case "$option" in
	d)	TETDIST="$OPTARG";;
	s)	SCRIPTS="$OPTARG";;
	t)	TESTDIR="$OPTARG";;
	v)	TESTVOL="$OPTARG";;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
if [ $# -lt 1 -o $# -gt 2 ]; then
	usage
fi
HOST=$1
if [ $# -gt 1 ]; then
	REPLHOST=$2
fi
#
# If the either test or TET directory doesn't start with a "/" then it's
# relative to the current directory.
#
if [ "${TESTDIR:0:1}" != "/" ]; then
	TESTDIR=`pwd`/${TESTDIR}
fi
#
# If the TET directory doesn't start with a "/" then it's relative to
# the current directory.
#
if [ "${TETDIST}" != "" -a "${TETDIST:0:1}" != "/" ]; then
	TETDIST=`pwd`/${TETDIST}
fi
# We need both a test and TET distribution directory to run.
if [ ! -d ${TESTDIR} -o ! -d ${TETDIST} -o ! -d ${TETDIST}/src/tet3 ]; then
	usage
fi

#
# Tar the tests and TET hierarchy to the master host.
#
(cd ${TETDIST}; tar cf - . | ssh root@${HOST} "cat >/tmp/tet.tar" )
(cd ${TESTDIR}; tar cf - . | ssh root@${HOST} "cat >/tmp/test.tar" )
#
# Copy our support scripts over.
#
scp ${SCRIPTS}/volcreate.sh root@${HOST}:/tmp
scp ${SCRIPTS}/zinstalltet.sh root@${HOST}:/tmp
scp ${SCRIPTS}/zruntet.sh root@${HOST}:/tmp
#
# If the base volumes don't exist, create them.
#
ssh root@${HOST} "/usr/bin/test -b /dev/${VGNAME}/${ORIGNAME}"
if [ $? -ne 0 ]; then
	ssh root@${HOST} "sh /tmp/volcreate.sh -n ${VGNAME} -o ${ORIGNAME} -s ${SNAPNAME} -O ${ORIGSIZE} -S ${SNAPSIZE}"
fi
#
# If the test Zumastor volume isn't already there, create it.
#
ssh root@${HOST} "/usr/bin/test -b /dev/mapper/${TESTVOL}"
if [ $? -ne 0 ]; then
	#
	# Set up the zumastor volume, create a file system on it and start making
	# snapshots.
	#
	ssh root@${HOST} "zumastor define volume ${TESTVOL} /dev/${VGNAME}/${ORIGNAME} /dev/${VGNAME}/${SNAPNAME} --initialize"
	ssh root@${HOST} "mkfs.ext3 /dev/mapper/${TESTVOL}"
	ssh root@${HOST} "zumastor define master ${TESTVOL} -h 24 -d 7"
	ssh root@${HOST} "zumastor snapshot ${TESTVOL} hourly"
fi
#
# Set up replication, if necessary.
#
if [ "${REPLHOST}" != "" ]; then
	sh ${SCRIPTS}/zreplicate.sh -s ${SCRIPTS} -v ${TESTVOL} ${HOST} ${REPLHOST}
fi
#
# Run the zinstalltet script to install and build TET.
#
ssh root@${HOST} "sh /tmp/zinstalltet.sh"
#
# Run the zruntet script to run the tests.
#
ssh root@${HOST} "sh /tmp/zruntet.sh"
