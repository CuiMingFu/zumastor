#!/bin/sh

# $Id$
#
# Tests run without privileges, other than those granted by tunbr, which
# must be installed first, along with interfaces-bridge.sh, proxy.sh, and
# dnsmasq.sh as described in cbtb/host-setup/README.
# Defaults to testing on dapper/i386 installed template images, DIST=etch
# and ARCH=i386 may also currently be specified.
#

set -e

if [ "x$ARCH" = "x" ] ; then
  ARCH=i386
fi

if [ "x$DIST" = "x" ] ; then
  DIST=dapper
fi

smoketests1="snapshot-zumastor-2045G.sh"
smoketests2="replication-zumastor.sh"
testparent=../tests

summary=`mktemp`

for test in $smoketests1
do
  if DIST=$DIST ARCH=$ARCH time tunbr ./test-zuma-uml.sh $testparent/1/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

for test in $smoketests2
do
  if  DIST=$DIST ARCH=$ARCH time tunbr tunbr ./test-zuma-uml.sh $testparent/2/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

echo
cat $summary
rm -f $summary
