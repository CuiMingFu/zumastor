#!/bin/sh -x
#
# $Id$
#
# Make use of large, extra devices on /dev/sd[bcd] to test
# multi-terabyte-sized zumastor filesystems.  RAID0 and LVM are used to
# create the large filesystems from 2030G disks provided by qemu (the
# largest supported by the qcow2 disk backing format).
# This is otherwise the same as the snapshot-zumastor-xfs.sh test
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# Expect failure.  Delete this when the test is fully successful.
# Read only by the test harness.
EXPECT_FAIL=1

# The required sizes of the sd[bcd] devices in G.
# Read only by the test harness.
HDBSIZE=2030
HDCSIZE=2030
HDDSIZE=2030

# Terminate test in 20 minutes.  Read by test harness.
TIMEOUT=1200

# necessary at the moment, looks like a zumastor bug
SLEEP=5


# function to return the mountpoint path of a particular snapshot of a volume
# old function, pre-r1102
snapmountpoint()
{
  echo "/var/run/zumastor/$1($2)"
}


# additional software requirements of this test
aptitude install xfsprogs
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

zumastor define volume testvol /dev/testvg/test /dev/testvg/test_snap --initialize
mkfs.xfs /dev/mapper/testvol

  # TODO: make this part of the zumastor define master or define volume
  # mkdir /var/lib/zumastor/volumes/testvol/filesystem
  echo nouuid >/var/lib/zumastor/volumes/testvol/filesystem/options

zumastor define master testvol -h 24 -d 7

echo ok 1 - testvol set up

sync
zumastor snapshot testvol hourly 
sleep $SLEEP

date >> /var/run/zumastor/mount/testvol/testfile
sleep $SLEEP

mountpoint0=$(snapmountpoint testvol 0)
if [ -d "$mountpoint0" ] ; then
  echo "ok 2 - first snapshot mounted"
else
  ls -lR /var/run/zumastor/mount
  cat /proc/mounts
  echo "not ok 2 - first snapshot mounted"
  exit 2
fi

if [ ! -f "$mountpoint0/testfile" ] ; then
  echo "ok 3 - testfile not present in first snapshot"
else
  ls -lR /var/run/zumastor/mount
  cat /proc/mounts
  echo "not ok 3 - testfile not present in first snapshot"
  exit 3
fi

sync
zumastor snapshot testvol hourly 
sleep $SLEEP

mountpoint2=$(snapmountpoint testvol 2)
if [ -d "$mountpoint2" ] ; then
  echo "ok 4 - second snapshot mounted"
else
  ls -lR /var/run/zumastor/mount
  cat /proc/mounts
  echo "not ok 4 - second snapshot mounted"
  exit 4
fi

if diff -q /var/run/zumastor/mount/testvol/testfile \
    "$mountpoint2/testfile" 2>&1 >/dev/null ; then
  echo "ok 5 - identical testfile immediately after second snapshot"
else
  ls -lR /var/run/zumastor/mount
  cat /proc/mounts
  echo "not ok 5 - identical testfile immediately after second snapshot"
  exit 5
fi

date >> /var/run/zumastor/mount/testvol/testfile

if ! diff -q /var/run/zumastor/mount/testvol/testfile \
    "$mountpoint2/testfile" 2>&1 >/dev/null ; then
  echo "ok 6 - testfile changed between origin and second snapshot"
else
  ls -lR /var/run/zumastor/mount
  cat /proc/mounts
  echo "not ok 6 - testfile changed between origin and second snapshot"
  exit 6
fi

exit 0
