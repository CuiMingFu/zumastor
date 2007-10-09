#!/bin/sh

set -x

#
# We don't expect or want any arguments.
#
if [ $# -ge 1 ]; then
	usage
fi
#
# Add the tet user if it doesn't already exist.
#
grep tet /etc/passwd
if [ $? -ne 0 ]; then
	adduser --system tet
fi
#
# Extract the tarfiles into the tet home directory.
#
cd /home/tet
mkdir TET
cd TET
tar xf /tmp/tet.tar
mkdir TETtests
cd TETtests
tar xf /tmp/test.tar
#
# Build TET.
#
cd ..
sh configure -t lite
cd src
make clean && make && make install
#
# Build tetreport.
#
cd ../contrib/tetreport
make
make install

exit 0
