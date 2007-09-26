#!/bin/sh -x
#
# $Id$
#
# Set up sysvg with origin and snapshot store.  Export the testvol
# over NFS, then verify that shutdown completes.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=1200

# necessary at the moment, looks like a zumastor bug
SLEEP=5

echo "1..4"

lvcreate --size 4m -n test sysvg
lvcreate --size 8m -n test_snap sysvg
zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize
mkfs.ext3 /dev/mapper/testvol
zumastor define master testvol -h 24 -d 7

echo ok 1 - testvol set up
sleep $SLEEP
if [ -d /var/run/zumastor/mount/testvol/ ] ; then
  echo ok 2 - testvol mounted
else
  echo "not ok 2 - testvol mounted"
  exit 2
fi

sync ; zumastor snapshot testvol hourly 
sleep $SLEEP
if [ -d /var/run/zumastor/mount/testvol\(0\)/ ] ; then
  echo ok 3 - testvol snapshotted
else
  echo "not ok 3 - testvol snapshotted"
  exit 3
fi
    
aptitude install -y nfs-kernel-server
echo "/var/run/zumastor/mount/testvol ${IPADDR}(rw,sync)" >>/etc/exports
/etc/init.d/nfs-common restart
/etc/init.d/nfs-kernel-server restart
if showmount -e ; then
  echo ok 4 - testvol exported
else
  echo not ok 4 - testvol exported
  exit 4
fi

exit 0
