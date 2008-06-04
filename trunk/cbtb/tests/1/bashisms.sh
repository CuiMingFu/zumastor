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

# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=600

NUMDEVS=2
DEV1SIZE=4
DEV2SIZE=8
#DEV1NAME=/dev/null
#DEV2NAME=/dev/null

# wait for file.  The first argument is the timeout, the second the file.
timeout_file_wait() {
  local max=$1
  local file=$2
  local count=0
  while [ ! -e $file ] && [ $count -lt $max ]
  do
    count=$(($count + 1))
    sleep 1
  done
  [ -e $file ]
  return $?
}

apt-get -q update || apt-get -q update || apt-get -q update

mkfs='mkfs.ext3 -F'
apt-get -q -y install e2fsprogs devscripts


  echo "1..7"
output=`checkbashisms /bin/zumastor /etc/cron.hourly/zumastor /etc/init.d/zumastor /etc/cron.weekly/zumastor /etc/cron.daily/zumastor`
if [ "x" != "$output" ]
then
  echo "not ok 1 - Zumastor contains bashisms! FAIL!"
  exit 1
else
  echo "ok 1 - checkbashisms returned clean"
fi

# If we get this far, be a little more destructive.
# install dash and replace /bin/sh
apt-get install -y dash && update-alternatives --install /bin/sh sh /bin/dash 1

# The installed zumastor scripts
for f in /bin/zumastor /etc/cron.hourly/zumastor /etc/init.d/zumastor \
    /etc/cron.weekly/zumastor /etc/cron.daily/zumastor
do
  sed -i 's:^#!/bin/bash:#!/bin/sh:' $f
done


  zumastor define volume testvol $DEV1NAME $DEV2NAME --initialize

  $mkfs /dev/mapper/testvol
  zumastor define master testvol -s; zumastor define schedule testvol -h 24 -d 7

  echo ok 2 - testvol set up

  sync
  zumastor snapshot testvol hourly


  if timeout_file_wait 30 /var/run/zumastor/snapshot/testvol/hourly.0 ; then
    echo "ok 3 - first snapshot mounted"
  else
    ls -lR /var/run/zumastor/
    echo "not ok 3 - first snapshot mounted"
    exit 3
  fi
        
  date >> /var/run/zumastor/mount/testvol/testfile

  if [ ! -f /var/run/zumastor/mount/testvol/hourly.0/testfile ] ; then
    echo "ok 4 - testfile not present in first snapshot"
  else
    ls -laR /var/run/zumastor/mount
    echo "not ok 4 - testfile not present in first snapshot"
    exit 4
  fi

  sync
  zumastor snapshot testvol hourly 

  if [ ! -f /var/run/zumastor/mount/testvol/hourly.1/testfile ] ; then
    echo "ok 5 - testfile not present in first snapshot"
  else
    ls -laR /var/run/zumastor/mount
    echo "not ok 5 - testfile not present in first snapshot"
    exit 5
  fi


  if diff -q /var/run/zumastor/mount/testvol/testfile \
      /var/run/zumastor/mount/testvol/hourly.0/testfile 2>&1 >/dev/null ; then
    echo "ok 6 - identical testfile immediately after second snapshot"
  else
    ls -lR /var/run/zumastor/mount
    echo "not ok 6 - identical testfile immediately after second snapshot"
    exit 6
  fi

  date >> /var/run/zumastor/mount/testvol/testfile

  if ! diff -q /var/run/zumastor/mount/testvol/testfile \
      /var/run/zumastor/mount/hourly.0/testfile 2>&1 >/dev/null ; then
    echo "ok 7 - testfile changed between origin and second snapshot"
  else
    ls -lR /var/run/zumastor/mount
    echo "not ok 7 - testfile changed between origin and second snapshot"
    exit 7
  fi

  zumastor forget volume testvol


exit 0
