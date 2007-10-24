#!/bin/sh -x
#
# $Id$
#
# Use ddsnap directly to create a snapshot, modify the origin, and create
# another snapshot.  Verify that checksums change and remain correct.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

rc=0


# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=2400

slave=${IPADDR2}
SSH='ssh -o StrictHostKeyChecking=no -o BatchMode=yes'
SCP='scp -o StrictHostKeyChecking=no -o BatchMode=yes'


# necessary at the moment, ddsnap just sends requests and doesn't wait
SLEEP=10

echo "1..25"

echo ${IPADDR} master >>/etc/hosts
echo ${IPADDR2} slave >>/etc/hosts
hostname master
ssh-keyscan -t rsa slave >>${HOME}/.ssh/known_hosts
ssh-keyscan -t rsa master >>${HOME}/.ssh/known_hosts
echo ok 1 - master network set up

echo ${IPADDR} master | ${SSH} root@${slave} "cat >>/etc/hosts"
echo ${IPADDR2} slave | ${SSH} root@${slave} "cat >>/etc/hosts"
${SCP} ${HOME}/.ssh/known_hosts root@${slave}:${HOME}/.ssh/known_hosts
${SSH} root@${slave} hostname slave
echo ok 2 - slave network set up

lvcreate --size 4m -n test sysvg
lvcreate --size 8m -n test_snap sysvg
dd if=/dev/zero bs=32k count=128 of=/dev/sysvg/test
dd if=/dev/zero bs=32k count=256 of=/dev/sysvg/test_snap
echo ok 3 - master lvm set up

${SSH} root@${slave} lvcreate --size 4m -n test sysvg
${SSH} root@${slave} lvcreate --size 8m -n test_snap sysvg
${SSH} root@${slave} dd if=/dev/zero bs=32k count=128 of=/dev/sysvg/test
${SSH} root@${slave} dd if=/dev/zero bs=32k count=256 of=/dev/sysvg/test_snap
echo ok 4 - slave lvm set up

ddsnap initialize /dev/sysvg/test_snap /dev/sysvg/test
echo ok 5 - master ddsnap initialize

controlsocket="/tmp/control"
ddsnap agent $controlsocket
echo ok 6 - master ddsnap agent
sleep $SLEEP

volname=testvol
mkdir /tmp/server
# TODO: when b/892805 is fixed, last element of socket may be something
# other than $volname
serversocket="/tmp/server/$volname"
ddsnap server /dev/sysvg/test_snap /dev/sysvg/test $controlsocket $serversocket
echo ok 7 - master ddsnap server
sleep $SLEEP

${SSH} root@${slave} ddsnap initialize /dev/sysvg/test_snap /dev/sysvg/test
echo ok 8 - slave ddsnap initialize
sleep $SLEEP

${SSH} root@${slave} ddsnap agent $controlsocket
echo ok 9 - slave ddsnap agent
sleep $SLEEP

${SSH} root@${slave} \
  ddsnap server /dev/sysvg/test_snap /dev/sysvg/test $controlsocket /tmp/server
echo ok 10 - slave ddsnap server
sleep $SLEEP

size=`ddsnap status /tmp/server --size` 
echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test $controlsocket -1 | dmsetup create $volname
echo ok 11 - master create $volname
sleep $SLEEP

$SSH root@${slave} "echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test $controlsocket -1 | dmsetup create $volname"
echo ok 12 - slave create $volname

listenport=3333
$SSH root@${slave} \
  ddsnap delta listen --foreground /dev/mapper/$volname ${slave}:${listenport} & \
  listenpid=$!
echo ok 13 - slave ddsnap delta listening for snapshot deltas
sleep $SLEEP


tosnap=0
ddsnap create $serversocket $tosnap
echo ok 13 - ddsnap create $tosnap
sleep $SLEEP

echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test \
  $controlsocket $tosnap | \
  dmsetup create $volname\($tosnap\)
echo ok 14 - create $volname\($tosnap\) block device on master
sleep $SLEEP

hash=`md5sum </dev/mapper/$volname`
hash0=`md5sum </dev/mapper/$volname\($tosnap\)`
if [ "$hash" != "$hash0" ] ; then
  echo -e "not "
  rc=15
fi
echo ok 15 - $volname==$volname\($tosnap\)
sleep $SLEEP

ddsnap transmit $serversocket ${slave}:$listenport $tosnap
echo ok 16 - snapshot $tosnap transmitting to slave
sleep $SLEEP

$SSH root@$slave \
  "echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test $controlsocket $tosnap | dmsetup create $volname\($tosnap\)"
echo ok 17 - create $volname\($tosnap\) block device on slave

hash0slave=`$SSH root@$slave "md5sum </dev/mapper/$volname\($tosnap\)"`
if [ "$hash0" != "$hash0slave" ] ; then
  echo -e "not "
  rc=18
fi
echo ok 18 - master $volname\($tosnap\) == slave $volname\($tosnap\)


dd if=/dev/urandom bs=32k count=128 of=/dev/mapper/$volname  
echo 19 - copy random data onto master $volname

fromsnap=0
tosnap=2
ddsnap create /tmp/server $tosnap
echo ok 20 - ddsnap create $tosnap
sleep $SLEEP

echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test \
  $controlsocket $tosnap | \
  dmsetup create $volname\($tosnap\)
echo ok 21 - create $volname\($tosnap\) block device on master

hash=`md5sum </dev/mapper/$volname`
hash2=`md5sum </dev/mapper/$volname\($tosnap\)`
if [ "$hash" != "$hash2" ] ; then
  echo -e "not "
  rc=22
fi
echo ok 22 - $volname==$volname\($tosnap\)

ddsnap transmit $controlsocket ${slave}:$listenport $fromsnap $tosnap
echo ok 23 - snapshot $tosnap transmitting to slave, delta from $fromsnap
sleep $SLEEP

$SSH root@$slave \
  "echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test $controlsocket $tosnap | dmsetup create $volname\($tosnap\)"
echo ok 24 - create $volname\($tosnap\) block device on slave

hash2slave=`$SSH root@$slave "md5sum </dev/mapper/$volname\($tosnap\)"`
if [ "$hash2" != "$hash2slave" ] ; then
  echo -e "not "
  rc=25
fi
echo ok 25 - master $volname\($tosnap\) == slave $volname\($tosnap\)


exit $rc

