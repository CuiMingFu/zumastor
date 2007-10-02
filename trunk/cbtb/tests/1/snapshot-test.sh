#!/bin/sh -x
#
# $Id$
#
# Set up sysvg with origin and snapshot store, iterate through a few
# snapshots to verify that each is unique and stable when taken.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=1200

# necessary at the moment, looks like a zumastor bug
SLEEP=5

echo "1..6"

lvcreate --size 1m -n test sysvg
lvcreate --size 2m -n test_snap sysvg
zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize
mkfs.ext3 /dev/mapper/testvol
zumastor define master testvol -h 24 -d 7

echo ok 1 - testvol set up

sync ; zumastor snapshot testvol hourly 

date >> /var/run/zumastor/mount/testvol/testfile
sleep $SLEEP

if [ -d /var/run/zumastor/mount/testvol\(0\)/ ] ; then
  echo "ok 2 - first snapshot mounted"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 2 - first snapshot mounted"
  exit 2
fi

if [ ! -f /var/run/zumastor/mount/testvol\(0\)/testfile ] ; then
  echo "ok 3 - testfile not present in first snapshot"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 3 - testfile not present in first snapshot"
  exit 3
fi

sync ; zumastor snapshot testvol hourly 
sleep $SLEEP

if [ -d /var/run/zumastor/mount/testvol\(2\)/ ] ; then
  echo "ok 4 - second snapshot mounted"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 4 - second snapshot mounted"
  exit 4
fi

if diff -q /var/run/zumastor/mount/testvol/testfile \
    /var/run/zumastor/mount/testvol\(2\)/testfile 2>&1 >/dev/null ; then
  echo "ok 5 - identical testfile immediately after second snapshot"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 5 - identical testfile immediately after second snapshot"
  exit 5
fi

date >> /var/run/zumastor/mount/testvol/testfile

if ! diff -q /var/run/zumastor/mount/testvol/testfile \
    /var/run/zumastor/mount/testvol\(2\)/testfile 2>&1 >/dev/null ; then
  echo "ok 6 - testfile changed between origin and second snapshot"
else
  ls -lR /var/run/zumastor/mount
  echo "not ok 6 - testfile changed between origin and second snapshot"
  exit 6
fi

exit 0
