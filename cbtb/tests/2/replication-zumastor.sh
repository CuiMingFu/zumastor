#!/bin/sh -x
#
# $Id$
#
# Set up origin and snapshot store on master and secondary machine on
# raw disks.  Begin replication cycle between machines
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
HDBSIZE=4
HDCSIZE=8

slave=${IPADDR2}

SSH='ssh -o StrictHostKeyChecking=no -o BatchMode=yes'
SCP='scp -o StrictHostKeyChecking=no -o BatchMode=yes'


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

# wait for file on remote site with timeout.
timeout_remote_file_wait() {
  local max=$1
  local remote=$2
  local file=$3
  local count=0
  while $SSH $remote [ ! -e $file ] && [ $count -lt $max ]
  do 
    let "count = count + 1"
    sleep 1
  done
  $SSH $remote [ -e $file ]
  return $?
}


echo "1..9"

echo ${IPADDR} master >>/etc/hosts
echo ${IPADDR2} slave >>/etc/hosts
hostname master
zumastor define volume testvol /dev/sdb /dev/sdc --initialize
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
${SSH} root@${slave} zumastor define volume testvol /dev/sdb /dev/sdc --initialize
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

# reasonable wait for these small volumes to finish the initial replication
if ! timeout_remote_file_wait 120 root@${slave} /var/run/zumastor/mount/testvol
then
  $SSH root@${slave} ls -alR /var/run/zumastor
  $SSH root@${slave} zumastor status --usage
  $SSH root@${slave} tail -200 /var/log/syslog
  
  echo not ok 6 - replication manually from master
  exit 6
else
  echo ok 6 - replication manually from master
fi



date >>/var/run/zumastor/mount/testvol/testfile
sync
zumastor snapshot testvol hourly 

if ! timeout_file_wait 30 /var/run/zumastor/mount/testvol
then
  ls -alR /var/run/zumastor
  zumastor status --usage
  tail -200 /var/log/syslog
  echo not ok 7 - testfile written, synced, and snapshotted
  exit 7
else
  echo ok 7 - testfile written, synced, and snapshotted
fi

hash=`md5sum /var/run/zumastor/mount/testvol/testfile`

#
# schedule an immediate replication cycle
#
zumastor replicate testvol slave


# give it two minutes to replicate (on a 30 second cycle), and verify
# that it is there.  If not, look at the target volume
if ! timeout_remote_file_wait 120 root@${slave} /var/run/zumastor/mount/testvol
then
  $SSH root@${slave} ls -alR /var/run/zumastor
  $SSH root@${slave} zumastor status --usage
  $SSH root@${slave} tail -200 /var/log/syslog
  
  echo not ok 8 - testfile has migrated to slave
  exit 8
else
  echo ok 8 - testfile has migrated to slave
fi

rhash=`${SSH} root@${slave} md5sum /var/run/zumastor/mount/testvol/testfile` || \
  ${SSH} root@${slave} <<EOF
    mount
    df
    ls -lR /var/run/zumastor/
    tail -200 /var/log/syslog
EOF


if [ "$rhash" = "$hash" ] ; then
  echo ok 9 - origin and slave testfiles are in sync
else
  echo not ok 9 - origin and slave testfiles are in sync
    mount
    df
    ls -lR /var/run/zumastor/
    tail -200 /var/log/syslog
  ${SSH} root@${slave} <<EOF
    mount
    df
    zumastor status --usage
    ls -lR /var/run/zumastor/
    tail -200 /var/log/syslog
EOF
  exit 9
fi

exit 0
