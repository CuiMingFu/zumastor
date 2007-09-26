#!/bin/sh -x
#
# $Id$
#
# Set up sysvg with origin and snapshot store on master and secondary machine.
# Begin replication cycle between machines
# and wait until it arrives and can be verified.
# Modify the origin and verify that the modification also arrives at the
# backup.
#
# Requires that the launch environment (eg. test-zuma-dapper-i386.sh) export
# both $IPADDR and $IPADDR2 to the paramter scripts.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# Terminate test in 20 minutes.  Read by test harness.
TIMEOUT=1200

slave=${IPADDR2}

SSH='ssh -o StrictHostKeyChecking=no -o BatchMode=yes'
SCP='scp -o StrictHostKeyChecking=no -o BatchMode=yes'


# necessary at the moment, looks like a zumastor bug
SLEEP=5

echo "1..4"

echo ${IPADDR} master >>/etc/hosts
echo ${IPADDR2} slave >>/etc/hosts
hostname master
lvcreate --size 4m -n test sysvg
lvcreate --size 8m -n test_snap sysvg
zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize
mkfs.ext3 /dev/mapper/testvol
zumastor define master testvol -h 24 -d 7
zumastor status --usage
ssh-keyscan -t rsa slave >>${HOME}/.ssh/known_hosts
ssh-keyscan -t rsa master >>${HOME}/.ssh/known_hosts
echo ok 1 - master testvol set up

echo ${IPADDR} master | ${SSH} root@${slave} "cat >>/etc/hosts"
echo ${IPADDR2} slave | ${SSH} root@${slave} "cat >>/etc/hosts"
${SCP} ${HOME}/.ssh/known_hosts root@${slave}:${HOME}/.ssh/known_hosts
${SSH} root@${slave} hostname slave
${SSH} root@${slave} lvcreate --size 4m -n test sysvg
${SSH} root@${slave} lvcreate --size 8m -n test_snap sysvg
${SSH} root@${slave} zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize
${SSH} root@${slave} zumastor status --usage
echo ok 2 - slave testvol set up
 
zumastor define target testvol slave:11235 -p 30
zumastor status --usage
echo ok 3 - defined target on master

${SSH} root@${slave} zumastor define source testvol master --period 600
${SSH} root@${slave} zumastor status --usage
echo ok 4 - configured source on target

${SSH} root@${slave} zumastor start source testvol
${SSH} root@${slave} zumastor status --usage
echo ok 5 - replication started on slave

zumastor replicate testvol slave
zumastor status --usage
echo ok 6 - replication manually kicked off from master

# reasonable wait for these small volumes to finish the initial replication
${SSH} root@${slave} ls -l /var/run/zumastor/mount/testvol

date >>/var/run/zumastor/mount/testvol/testfile
sync
zumastor snapshot testvol hourly 
sleep 2
zumastor status --usage
${SSH} root@${slave} ls -l /var/run/zumastor/mount/testvol
${SSH} root@${slave} zumastor status --usage

echo ok 7 - testfile written, synced, and snapshotted

hash=`md5sum /var/run/zumastor/mount/testvol/testfile`

# give it a minute to replicate (on a 30 second cycle), and verify
# that it is there.  If not, look at the target volume, wait 5 minutes,
# and look again
sleep 60
rhash=`${SSH} root@${slave} md5sum /var/run/zumastor/mount/testvol/testfile` || \
  ${SSH} root@${slave} <<EOF
    mount
    df
    ls -lR /var/run/zumastor/mount/
    tail -200 /var/log/syslog
    sleep 300
    mount
    df
    ls -lR /var/run/zumastor/mount/
EOF


if [ "$rhash" = "$hash" ] ; then
  echo ok 8 - origin and slave testfiles are in sync
else
  echo not ok 8 - origin and slave testfiles are in sync
  exit 8
fi

exit 0
