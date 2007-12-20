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
TIMEOUT=600

# necessary at the moment, looks like a zumastor bug
SLEEP=5

mkfs='mkfs.xfs -f'
aptitude install xfsprogs

lvcreate --size 16m -n test sysvg
lvcreate --size 16m -n test_snap sysvg

  echo "1..6"

  zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize --mountopts nouuid
  $mkfs /dev/mapper/testvol
  zumastor define master testvol -h 24 -d 7

  echo ok 1 - testvol set up

  sync
  zumastor snapshot testvol hourly 
  sleep $SLEEP
  
  date >> /var/run/zumastor/mount/testvol/testfile
  sleep $SLEEP

  if [ -d /var/run/zumastor/mount/testvol/.snapshot/hourly.0/ ] ; then
    echo "ok 2 - first snapshot mounted"
  else
    ls -laR /var/run/zumastor/mount
    echo "not ok 2 - first snapshot mounted"
    exit 2
  fi

  if [ ! -f /var/run/zumastor/mount/testvol/.snapshot/hourly.0/testfile ] ; then
    echo "ok 3 - testfile not present in first snapshot"
  else
    ls -laR /var/run/zumastor/mount
    echo "not ok 3 - testfile not present in first snapshot"
    exit 3
  fi

  sleep $SLEEP
  sync
  zumastor snapshot testvol hourly 
  sleep $SLEEP

  if [ -d /var/run/zumastor/mount/testvol/.snapshot/hourly.1/ ] ; then
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
      /var/run/zumastor/mount/.snapshot/hourly.0/testfile 2>&1 >/dev/null ; then
    echo "ok 6 - testfile changed between origin and second snapshot"
  else
    ls -lR /var/run/zumastor/mount
    echo "not ok 6 - testfile changed between origin and second snapshot"
    exit 6
  fi

  zumastor forget volume testvol

lvremove -f /dev/sysvg/test
lvremove -f /dev/sysvg/test_snap

exit 0
