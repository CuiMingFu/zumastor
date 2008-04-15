#!/bin/sh -x
#
# $Id$
#
# Set up a zumastor volume and verify that --zero really zeroes it out.
#
# Copyright 2007-2008 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# The required sizes of the sdb and sdc devices in M.
# Read only by the test harness.
HDBSIZE=4
HDCSIZE=8

# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=600

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



echo "1..4"

apt-get update

dd if=/dev/urandom bs=512 of=/dev/sdb || true
dd if=/dev/urandom bs=512 of=/dev/sdc || true
echo ok 1 - raw volumes randomized

zumastor define volume testvol /dev/sdb /dev/sdc --initialize  --zero
echo ok 2 - zumastor volume defined

size=`blockdev --getsize64 /dev/mapper/testvol`
echo ok 3 - got testvol size

if cmp --bytes=$size /dev/mapper/testvol /dev/zero
then
  echo ok 4 - testvol matches /dev/zero
else
  echo not ok 4 - testvol does not match /dev/zero
  exit 4
fi

exit 0
