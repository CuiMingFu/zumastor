#!/bin/sh -x
#
# $Id$
#
# Make use of large, extra devices on /dev/sd[bcd] to test
# multi-terabyte-sized zumastor filesystems.  RAID0 and LVM are used to
# create the large filesystems from 2045G disks provided by qemu (the
# largest supported by the 32-bit UML disk backing format).
# This is otherwise the same as the snapshot-zumastor-xfs.sh test
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# Expect failure.  Delete this when the test is fully successful.
# Read only by the test harness.
EXPECT_FAIL=1

# The required sizes of the sd[bcd] devices in M.
# Read only by the test harness.
HDBSIZE=2094080
HDCSIZE=2094080
HDDSIZE=2094080

# Terminate test in 20 minutes.  Read by test harness.
TIMEOUT=1200

# necessary at the moment, looks like a zumastor bug
SLEEP=5



# additional software requirements of this test
apt-get update
aptitude install -y xfsprogs
modprobe xfs

# create LVM VG testvg
time pvcreate -ff /dev/sdb
time pvcreate -ff /dev/sdc
time pvcreate -ff /dev/sdd
time vgcreate testvg /dev/sdb /dev/sdc /dev/sdd

# create volumes 5T origin and .5T snapshot
time lvcreate --size 5124G -n test testvg
time lvcreate --size 512G -n test_snap testvg

echo "1..6"

zumastor define volume testvol /dev/testvg/test /dev/testvg/test_snap --initialize --mountopts nouuid
mkfs.xfs /dev/mapper/testvol
zumastor define master testvol -h 24 -d 7

echo ok 1 - testvol set up


sync
zumastor snapshot testvol hourly 
sleep $SLEEP

date >> /var/run/zumastor/mount/testvol/testfile
sleep $SLEEP

if [ ! -f /var/run/zumastor/mount/testvol/.snapshot/hourly.0/testfile ] ; then
  echo "ok 3 - testfile not present in first snapshot"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 3 - testfile not present in first snapshot"
  exit 3
fi

sync
zumastor snapshot testvol hourly 
sleep $SLEEP

if [ -e /var/run/zumastor/mount/testvol/.snapshot/hourly.1/ ] ; then
  echo "ok 4 - second snapshot mounted"
else
  ls -laR /var/run/zumastor/mount
  echo "not ok 4 - second snapshot mounted"
  exit 4
fi

if diff -q /var/run/zumastor/mount/testvol/testfile \
    /var/run/zumastor/mount/testvol/.snapshot/hourly.0/testfile 2>&1 >/dev/null ; then
  echo "ok 5 - identical testfile immediately after second snapshot"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 5 - identical testfile immediately after second snapshot"
  exit 5
fi

date >> /var/run/zumastor/mount/testvol/testfile

if ! diff -q /var/run/zumastor/mount/testvol/testfile \
    /var/run/zumastor/mount/testvol/.snapshot/hourly.0/testfile 2>&1 >/dev/null ; then
  echo "ok 6 - testfile changed between origin and second snapshot"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 6 - testfile changed between origin and second snapshot"
  exit 6
fi

exit 0
