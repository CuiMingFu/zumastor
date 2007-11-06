#!/bin/sh

set -x

# Name of the Zumastor volume.
TESTVOL=testvol

usage()
{
	echo "Usage: $0 [-v <test volume>]" 1>&2
	echo "Where <test volume> is the name of the volume that will be used for testing."
	exit 1
}

while getopts "v:" option ; do
	case "$option" in
	v)	TESTVOL="$OPTARG";;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
if [ $# -ge 1 ]; then
	usage
fi

#
# If the tet user doesn't exist, die.
#
grep tet /etc/passwd
if [ $? -ne 0 ]; then
	exit 5
fi

#
# Set up TET environment, go there and run it.
#
PATH=$PATH:/home/tet/TET/bin
export TET_ROOT=/home/tet/TET
cd /home/tet/TET
outfile=/tmp/tcc-out.$$
#
# The tests are subdirectories of TETtests, walk that list.
#
for TEST in TETtests/*; do
	#
	# Build the test set.
	#
	echo "Building test set ${TEST}."
	tcc -p -b ${TEST} >${outfile}
	JOURNAL=`grep 'journal file is' ${outfile} | sed -e "s/^.*file is //"`
	if [ -z "${JOURNAL}" -o ! -f "${JOURNAL}" ]; then
		echo "TET test build journal ('${JOURNAL}') not found for test set ${TEST}!" >&2
		exit 6
	fi
	grep "make.*error" ${JOURNAL}
	if [ $? -eq 0 ]; then
		echo "Error building TET test set ${TEST}, see ${JOURNAL} for details." >&2
		exit 6
	fi
	#
	# The build must have succeeded, now run the test set.
	#
	echo "Running test set ${TEST}."
	tcc -p -e ${TEST} >${outfile}
	JOURNAL=`grep 'journal file is' ${outfile} | sed -e "s/^.*file is //"`
	if [ -z "${JOURNAL}" -o ! -f "${JOURNAL}" ]; then
		echo "TET test execution journal ('${JOURNAL}') not found for test set ${TEST}!" >&2
		exit 6
	fi
	echo "Test set ${TEST} run, see ${JOURNAL} for results."
	#
	# Run tetreport (if available) to summarize results.
	#
	if [ -x bin/tetreport ]; then
		tetreport -f ${JOURNAL}
	fi
done

exit 0
