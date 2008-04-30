#!/bin/bash -x

# $Id: smoke.sh 1198 2007-12-22 11:43:15Z drake.diedrich $
#
# build-i386.sh requires root privs via sudo, so this smoke test may
# only be run interactively and is inherently somewhat dangerous.
# You should inspect the scripts before running them.
#
# Several portions of this script require your host have some of the cbtb
# setup scripts.  See cbtb/host-setup/README, and in particular
# interfaces-bridge.sh, proxy.sh, and dnsmasq.sh.
#
#

if [ "x$LINENO" = "x" ]
then
  echo "Looks like you are not using bash"
  echo "Please re-run with bash"
  exit 1
fi

ARCH=`dpkg --print-architecture`
DIST=etch
time ./setup.sh
time ./build-${DIST}-${ARCH}.sh
retval=$?
if [ $retval != 0 ]
then
  echo "Build script failed"
  exit $retval
fi
time env DIST=${DIST} ARCH=${ARCH} ./runtests.sh $*
