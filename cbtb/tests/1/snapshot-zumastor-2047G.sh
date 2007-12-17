#!/bin/sh -x
#
# $Id$
#
# Make use of large, extra devices on /dev/sdb and /dev/sdc to test
# terabyte-sized zumastor filesystems.  Other than not using LVM and using
# very large devices, this is the same as the snapshot-zumastor.sh test.
# To reduce the runtime of this test, only the XFS filesystem is tested.
# mkfs.ext3 takes on the order of a couple of hours to run under emulation
# on a filesystem of this size.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# The required sizes of the sdb and sdc devices in G.
# Read only by the test harness.
HDBSIZE=2044
HDCSIZE=2044

# Terminate test in 40 minutes.  Read by test harness.
TIMEOUT=2400

# necessary at the moment, looks like a zumastor bug
SLEEP=5

echo "1..6"

zumastor define volume testvol /dev/sdb /dev/sdc --initialize
mkfs.ext3 /dev/mapper/testvol

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
