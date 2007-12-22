#!/bin/sh

# $Id$
#
# Tests run without privileges, other than those granted by tunbr, which
# must be installed first, along with interfaces-bridge.sh, proxy.sh, and
# dnsmasq.sh as described in cbtb/host-setup/README.
#

smoketests1="snapshot-zumastor-2045G.sh"
smoketests2="replication-zumastor.sh"
testparent=../tests

summary=`mktemp`

for test in $smoketests1
do
  if time tunbr ./test-zuma-uml-dapper-i386.sh $testparent/1/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

for test in $smoketests2
do
  if time tunbr tunbr ./test-zuma-uml-dapper-i386.sh $testparent/2/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

echo
cat $summary
rm -f $summary
