#!/bin/sh -x
#
# $Id: snapshot-zumastor-ext3-resize.sh 1189 2007-12-22 00:27:19Z jiahotcake $
#
# Set up testvg with origin and snapshot store, resize origin/snapshot volumes,
# verify resize succeeds and data on origin/snapshot volumes still valid after resizing
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Jiaying Zhang (jiayingz@google.com)


set -e

# The required sizes of the sdb and sdc devices in M.
# Read only by the test harness.
HDBSIZE=1024

# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=600

# feature not yet implemented
EXPECT_FAIL=1

# wait for file.  The first argument is the timeout, the second the file.
timeout_file_wait() {
  local max=$1
  local file=$2
  local count=0
  while [ ! -e $file ] && [ $count -lt $max ]
  do
    let "count = count + 1"
    sleep 1
  done
  [ -e $file ]
  return $?
}

function file_check {
  if diff -q /var/run/zumastor/mount/testvol/testfile \
      /var/run/zumastor/mount/testvol/.snapshot/hourly.0/testfile 2>&1 >/dev/null ; then
    echo "ok $1 - $2"
  else
    ls -lR /var/run/zumastor/mount
    echo "not ok $1 - $2"
    exit $1
  fi
}

function size_check {
  if [ $1 = "origin" ]; then
  	realsize=`ddsnap status /var/run/zumastor/servers/testvol | awk '/Origin size:/ { print $3 }'`
  else
  	realsize=`ddsnap status /var/run/zumastor/servers/testvol | awk '/Snapshot store/ { print $9 }'`
  fi
  if [ $realsize = $2 ]; then
    echo "ok $3 - $4"
  else
    echo "not ok $3 - $4"
    exit $3
  fi
}

apt-get update
aptitude install -y e2fsprogs

# create LVM VG testvg
time pvcreate -ff /dev/sdb
time vgcreate testvg /dev/sdb

# create volumes 8M origin and 8M snapshot
time lvcreate --size 8M -n test testvg
time lvcreate --size 8M -n test_snap testvg

echo "1..10"

zumastor define volume testvol /dev/testvg/test /dev/testvg/test_snap --initialize

mkfs.ext3 /dev/mapper/testvol
zumastor define master testvol -h 24 -d 7 -s

echo ok 1 - testvol set up

date >> /var/run/zumastor/mount/testvol/testfile
sync
zumastor snapshot testvol hourly 

if timeout_file_wait 120 /var/run/zumastor/snapshot/testvol/hourly.0 ; then
  echo "ok 2 - first snapshot mounted"
else
  ls -laR /var/run/zumastor/mount
  echo "not ok 2 - first snapshot mounted"
  exit 2
fi

file_check 3 "identical testfile after snapshot"

# test origin volume enlarge
lvresize /dev/testvg/test -L 16m
/etc/init.d/zumastor restart
zumastor stop master testvol
e2fsck -f -y /dev/mapper/testvol
resize2fs /dev/mapper/testvol 16m
zumastor start master testvol
size_check "origin" "16,777,216" 4 "size check after origin volume enlarge"
file_check 5 "testfile changed after origin volume enlarge"

# test snapshot store enlarge
lvresize /dev/testvg/test_snap -L 64m
/etc/init.d/zumastor restart
# 1024 is the number of snapshot blocks with the defaul block size 16k
size_check "snap" "4,096" 6 "size check after snapshot store enlarge"
file_check 7 "testfile changed after snapshot store enlarge"
# we will have two snapshots mounted if snapshot enlarging succeeds
# otherwise, ddsnap server will automatically delete the old snapshot
dd if=/dev/zero of=/var/run/zumastor/mount/testvol/zerofile bs=1k count=12k
sync
zumastor snapshot testvol hourly
if timeout_file_wait 120 /var/run/zumastor/snapshot/testvol/hourly.1 ; then
  echo "ok 8 - two snapshots are mounted"
else
  ls -laR /var/run/zumastor/mount
  echo "not ok 8 - two snapshots are mounted"
  exit 2
fi
rm -f /var/run/zumastor/mount/testvol/zerofile
sync

# test origin volume shrink: currently not fully supported
zumastor stop master testvol
e2fsck -f -y /dev/mapper/testvol
resize2fs /dev/mapper/testvol 4m
# force any data left on the freed space to be copied out to the snapstore
dd if=/dev/zero of=/dev/mapper/testvol bs=1k seek=4k count=12k
sync
/etc/init.d/zumastor stop
echo y | lvresize /dev/testvg/test -L 4m
/etc/init.d/zumastor start
size_check "origin" "4,194,304" 9 "size check after origin volume shrink"
#file_check 9 "testfile changed after origin volume shrink"

# test snapshot store shrink: currently not fully supported
echo y | lvresize /dev/testvg/test_snap -L 32m
/etc/init.d/zumastor restart
size_check "snap" "2,048" 10 "size check after snapshot store shrink"
#file_check 11 "testfile changed after snapshot store shrink"

exit 0