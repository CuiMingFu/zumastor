#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# test zumastor stop on target

. config_uml
. config_single

# load up uml
./start_uml.sh $uml_fs $ubdb_dev $ubdc_dev $uml_host || { echo UNRESOLVED; exit 1; }

echo -n Setting up volume...
ssh $SSH_OPTS $uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc" >& $LOG
ssh $SSH_OPTS $uml_host "mkfs.ext3 /dev/mapper/$vol" >& $LOG
echo -e "done.\n"

mount_point=/var/run/zumastor/mount/$vol
RUNPATH=/var/run/zumastor
SERVERS=$RUNPATH/servers
AGENTS=$RUNPATH/agents
server=${SERVERS}/$vol
agent=${AGENTS}/$vol

echo "test origin device cleanup"
count=0
while [[ $count -lt $ITERATIONS ]]; do
	echo count $count
	ssh $SSH_OPTS $uml_host "mkdir -p $mount_point"
	ssh $SSH_OPTS $uml_host "mount /dev/mapper/$vol $mount_point" || { echo "mount origin device error $?"; echo FAIL; exit 1; }
	(ssh $SSH_OPTS $uml_host "dd if=/dev/zero of=$mount_point/zero >& /dev/null" &)
	sleep 6
	ssh $SSH_OPTS $uml_host "pkill -f 'ddsnap agent'"
	ssh $SSH_OPTS $uml_host "/etc/init.d/zumastor stop" >& $LOG || { echo "stop in origin test error $?"; echo FAIL; exit 1; }
	ssh $SSH_OPTS $uml_host "pkill -f 'dd if'"
	sleep 1
	ssh $SSH_OPTS $uml_host "dmsetup remove $vol"
	ssh $SSH_OPTS $uml_host "/etc/init.d/zumastor start" >& $LOG || { echo "start in origin test error $?"; echo FAIL; exit 1; }
	count=$(( count+1 ))
done

echo "test snapshot device cleanup"
ssh $SSH_OPTS $uml_host "ddsnap create $server 0"
size=$(ssh $SSH_OPTS $uml_host "ddsnap status $server --size") || echo FAIL

count=0
while [[ $count -lt $ITERATIONS ]]; do
	echo count $count
	ssh $SSH_OPTS $uml_host "echo 0 $size ddsnap /dev/ubdc /dev/ubdb $agent 0 | dmsetup create $vol\(0\)"
	ssh $SSH_OPTS $uml_host "mkdir -p $mount_point"
	ssh $SSH_OPTS $uml_host "mount /dev/mapper/$vol\(0\) $mount_point" || { echo "mount snapshot device error $?"; echo FAIL; exit 1; }
	(ssh $SSH_OPTS $uml_host "dd if=/dev/zero of=$mount_point/zero >& /dev/null" &)
	sleep 6
	ssh $SSH_OPTS $uml_host "pkill -f 'ddsnap agent'"
	ssh $SSH_OPTS $uml_host "/etc/init.d/zumastor stop" >& $LOG || { echo "stop in snapshot test error $?"; echo FAIL; exit 1; }
	ssh $SSH_OPTS $uml_host "pkill -f 'dd if'"
	sleep 1
	ssh $SSH_OPTS $uml_host "umount /dev/mapper/$vol\(0\)" || echo "umount snapshot device error $?"
	ssh $SSH_OPTS $uml_host "dmsetup remove $vol\(0\)" || echo "remove snapshot device error $?"
	ssh $SSH_OPTS $uml_host "/etc/init.d/zumastor start" >& $LOG || { echo "start in snapshot test error $?"; echo FAIL; exit 1; }
	count=$(( count+1 ))
done

ssh $SSH_OPTS $uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $uml_host "halt" >& /dev/null || { echo FAIL; exit 1; }
echo PASS
