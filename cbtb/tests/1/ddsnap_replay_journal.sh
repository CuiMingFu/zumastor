#!/bin/sh -x
#
# Verify ddsnap journal replay works correctly when
# 'ddsnap server' restarts after an unclean exit
#
# Copyright 2008 Google Inc.  All rights reserved
# Author: Jiaying Zhang (jiayingz@google.com)

set -e

NUMDEVS=2
DEV1SIZE=8
DEV2SIZE=4

echo "1..5"  # Five checking steps in this test

# these three environment variables are read by ddsnapd for fault injection
export DDSNAP_ACTION="abort"
export DDSNAP_TRIGGER="SHUTDOWN_SERVER"
export DDSNAP_COUNT=1

[ -e /tmp/srcsvr.log ] && rm /tmp/srcsvr.log
ddsnap initialize -y -c 8k $DEV1NAME $DEV2NAME

ddsnap agent --logfile /tmp/srcagt.log /tmp/src.control
ddsnap server --logfile /tmp/srcsvr.log $DEV1NAME $DEV2NAME /tmp/src.control /tmp/src.server -X -D
sleep 3

size=`ddsnap status /tmp/src.server --size`
echo $size
echo 0 $size ddsnap $DEV1NAME $DEV2NAME /tmp/src.control -1 | dmsetup create testvol

ddsnap create /tmp/src.server 0
blockdev --flushbufs /dev/mapper/testvol
echo 0 $size ddsnap $DEV1NAME $DEV2NAME /tmp/src.control 0 | dmsetup create testvol\(0\)

echo "ok 1 - ddsnap snapshot set up"

dd if=/dev/zero of=/dev/mapper/testvol bs=1K count=100 || exit 2
sync
pkill -f "ddsnap server"

# clean up the fault injection
export DDSNAP_ACTION=
export DDSNAP_TRIGGER=
export DDSNAP_COUNT=

# restart ddsnap server which will run journal replay
ddsnap agent --logfile /tmp/srcagt.log /tmp/src.control
ddsnap server --logfile /tmp/srcsvr.log $DEV1NAME $DEV2NAME /tmp/src.control /tmp/src.server
sleep 3

cat /tmp/srcsvr.log

grep "Replaying journal" /tmp/srcsvr.log || { echo "not ok 2 - journal replay triggered"; exit 2; }
echo "ok 2 - journal replay triggered"

grep "count wrong" /tmp/srcsvr.log && { echo "not ok 3 - freechunk check after journal replay"; exit 3; }
echo "ok 3 - freechunk check after journal replay"

grep "Journal recovery failed" /tmp/srcsvr.log && { echo "not ok 4 - journal replay succeeds"; exit 4; }
echo "ok 4 - journal replay succeeds"

ps -ef | grep ddsnap || { echo "not ok 5 - ddsnap server is running"; exit 5; }
echo "ok 5 - ddsnap server is running";

dmsetup remove testvol\(0\)
dmsetup remove testvol
pkill -f "ddsnap agent"
echo 'ok 5 - cleanup'

exit 0
