#!/bin/sh -x
#
# $Id$
#
# Export zumastor over NFS to a slave and then perform some filesystem
# actions.  Verify that the filesystem actions arrive in the snapshot store.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

rc=0

# Terminate test in 20 minutes.  Read by test harness.
TIMEOUT=2400

# necessary at the moment, looks like a zumastor bug
SLEEP=5

slave=${IPADDR2}

SSH='ssh -o StrictHostKeyChecking=no -o BatchMode=yes'
SCP='scp -o StrictHostKeyChecking=no -o BatchMode=yes'

echo "1..8"
echo ${IPADDR} master >>/etc/hosts
echo ${IPADDR2} slave >>/etc/hosts
hostname master
lvcreate --size 4m -n test sysvg
lvcreate --size 8m -n test_snap sysvg
zumastor define volume testvol /dev/sysvg/test /dev/sysvg/test_snap --initialize
mkfs.ext3 /dev/mapper/testvol
zumastor define master testvol -h 24 -d 7
ssh-keyscan -t rsa slave >>${HOME}/.ssh/known_hosts
ssh-keyscan -t rsa master >>${HOME}/.ssh/known_hosts
echo ok 1 - testvol set up

sleep $SLEEP
if [ -d /var/run/zumastor/mount/testvol/ ] ; then
  echo ok 2 - testvol mounted
else
  echo "not ok 2 - testvol mounted"
  exit 2
fi

sync ; zumastor snapshot testvol hourly 
sleep $SLEEP
if [ -d /var/run/zumastor/mount/testvol\(0\)/ ] ; then
  echo ok 3 - testvol snapshotted
else
  echo "not ok 3 - testvol snapshotted"
  exit 3
fi
    
aptitude install -y nfs-kernel-server
echo "/var/run/zumastor/mount/testvol slave(rw,sync,no_root_squash)" >>/etc/exports
/etc/init.d/nfs-common restart
/etc/init.d/nfs-kernel-server restart
if showmount -e ; then
  echo ok 4 - testvol exported
else
  echo not ok 4 - testvol exported
  exit 4
fi


echo ${IPADDR} master | ${SSH} root@${slave} "cat >>/etc/hosts"
echo ${IPADDR2} slave | ${SSH} root@${slave} "cat >>/etc/hosts"
${SCP} ${HOME}/.ssh/known_hosts root@${slave}:${HOME}/.ssh/known_hosts
${SSH} root@${slave} hostname slave
${SSH} root@${slave} aptitude install -y nfs-common
${SSH} root@${slave} mount master:/var/run/zumastor/mount/testvol /mnt
${SSH} root@${slave} mount
echo ok 5 - slave set up


date >> /var/run/zumastor/mount/testvol/masterfile
hash=`md5sum </var/run/zumastor/mount/testvol/masterfile`
rhash=`${SSH} root@${slave} 'md5sum </mnt/masterfile'`
if [ "x$hash" = "x$rhash" ] ; then
  echo ok 5 - file written on master matches NFS client view
else
  rc=5
  echo not ok 5 - file written on master matches NFS client view
fi

date | ${SSH} root@${slave} "cat >/mnt/clientfile"
hash=`md5sum </var/run/zumastor/mount/testvol/clientfile`
rhash=`${SSH} root@${slave} 'md5sum </mnt/clientfile'`
if [ "x$hash" = "x$rhash" ] ; then
  echo ok 6 - file written on NFS client visible on master
else
  rc=6
  echo not ok 6 - file written on NFS client visible on master
fi

  
rm /var/run/zumastor/mount/testvol/masterfile
if $SSH root@${slave} test -f /mnt/masterfile ; then
  rc=7
  echo not ok 7 - rm on master did show up on NFS client
else
  echo ok 7 - rm on master did show up on NFS client
fi

${SSH} root@${slave} rm /mnt/clientfile
if [ -f /mnt/masterfile ] ; then
  rc=8
  echo not ok 8 - rm on NFS client did show up on master
else
  echo ok 8 - rm on NFS client did show up on master
fi


exit $rc
