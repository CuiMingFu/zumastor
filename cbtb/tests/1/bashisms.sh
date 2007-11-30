#!/bin/sh -x
#
# $Id$
#
# Replace /bin/sh with dash in the dapper test harness, and then replace
# all of the #!/bin/bash in a list of zumastor scripts in order to see where
# zumastor breaks due to bashisms in the scripts.  This test is expected
# to *really* fail.  The base of this test is the zumastor snapshot test.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# no kidding
EXPECT_FAIL=1

# install dash and replace /bin/sh
aptitude install dash
ln -sf dash /bin/sh

# The installed zumastor scripts
for f in /bin/zumastor /etc/cron.hourly/zumastor /etc/init.d/zumastor \
    /etc/cron.weekly/zumastor /etc/cron.daily/zumastor
do
  sed -i 's/^#!\/bin\/bash/#!\/bin\/sh/' $f
done


# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=600

# necessary at the moment, looks like a zumastor bug
SLEEP=5




mkfs='mkfs.ext3 -F'
aptitude install e2fsprogs

lvcreate --size 4m -n test sysvg
lvcreate --size 4m -n test_snap sysvg

  echo "1..6"

  zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize

  $mkfs /dev/mapper/testvol
  zumastor define master testvol -h 24 -d 7

  echo ok 1 - testvol set up

  sync ; zumastor snapshot testvol hourly 
  sleep $SLEEP
 
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

  sleep $SLEEP
  sync
  sleep $SLEEP
  zumastor snapshot testvol hourly 
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

  zumastor forget volume testvol

lvremove -f /dev/sysvg/test
lvremove -f /dev/sysvg/test_snap

exit 0
