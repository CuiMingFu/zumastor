#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# This test is based on the failure injection scripts from Joel Votaw <jvotaw@google.com>
# It untars a series of kernel tarball, at the same time,
# simulates processes crashing and sysadmin mistakes at an accelerated rate

. config_uml
. config_replication

function resize_device {
	fsize=`stat -c %s $1`
	if [[ -z $fsize || $fsize -lt 524288000 ]]; then
		# don't auto resize lvm devices
		if [[  $1 == /dev/sysvg/* ]]; then
			echo "please set source and target volumes with proper access mode and size larger than 500M"
		else
			dd if=/dev/zero of=$1 count=512000 bs=1024 >& /dev/null && return 0
			echo "can not create device $1, error $?"
		fi
		ssh $SSH_OPTS $source_uml_host "halt"
		ssh $SSH_OPTS $target_uml_host "halt"
		echo UNRESOLVED
		exit 1
	fi
}

./start_replication.sh || { echo UNRESOLVED; exit 1; }

# check if volume size is larger than 500M for untar test
pushd $WORKDIR
echo -n Checking the volume size, resize if necessary ...
resize_device $source_ubdb_dev
resize_device $source_ubdc_dev
resize_device $target_ubdb_dev
resize_device $target_ubdc_dev
echo -e "done.\n"
popd

echo -n Copy necessary files to umls ...
ssh $SSH_OPTS $source_uml_host "ls /root/source-crontab /root/introduce_bugs.sh /root/untar_kernel_test.sh" >& /dev/null
status=$?
if [[ $status -ne 0 ]]; then
	scp $SCP_OPTS source-crontab root@$source_uml_host:/root/
	scp $SCP_OPTS introduce_bugs.sh root@$source_uml_host:/root/
	scp $SCP_OPTS untar_kernel_test.sh root@$source_uml_host:/root/
	ssh $SSH_OPTS $source_uml_host "chmod +x /root/introduce_bugs.sh /root/untar_kernel_test.sh"
fi
ssh $SSH_OPTS $target_uml_host "ls /root/target-crontab /root/introduce_bugs.sh" >& /dev/null
status=$?
if [[ $status -ne 0 ]]; then
	scp $SCP_OPTS target-crontab root@$target_uml_host:/root/
	scp $SCP_OPTS introduce_bugs.sh root@$target_uml_host:/root/
	ssh $SSH_OPTS $target_uml_host "chmod +x /root/introduce_bugs.sh"
fi
echo -e "done.\n"

echo -n Start replication ...
ssh $SSH_OPTS $source_uml_host "zumastor define target $vol $target_uml_host:$target_port -p 1" >& $LOG || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "zumastor define source $vol $source_uml_host -p $interval" >& $LOG || { echo FAIL; exit 1; }
echo -e "done.\n"
sleep 30

echo -n Installing crontab files ...
ssh $SSH_OPTS $source_uml_host "ls /var/spool/cron/crontabs/root" >& /dev/null
status=$?
if [[ $status -ne 0 ]]; then
	ssh $SSH_OPTS $source_uml_host "crontab /root/source-crontab"
	ssh $SSH_OPTS $source_uml_host "/root/untar_kernel_test.sh >/dev/null 2>&1 &"
fi

ssh $SSH_OPTS $target_uml_host "ls /var/spool/cron/crontabs/root" >& /dev/null
status=$?
if [[ $status -ne 0 ]]; then
	ssh $SSH_OPTS $target_uml_host "crontab /root/target-crontab"
fi
echo -e "done.\n"

count=0
while [[ $count -lt $ITERATIONS ]]; do
	echo count $count
	echo -n "source send  "
	ssh $SSH_OPTS $source_uml_host "cat $VOLUMES/$vol/targets/$target_uml_host/send"
	echo -n "target apply "
	ssh $SSH_OPTS $target_uml_host "cat $VOLUMES/$vol/source/apply"
	sleep 60
	count=$(( count+1 ))
done

echo -n Cleaning up ...
ssh $SSH_OPTS $source_uml_host "crontab -r"
ssh $SSH_OPTS $source_uml_host "rm -rf /root/linux-*"
ssh $SSH_OPTS $target_uml_host "crontab -r"
echo -e "done.\n"

./stop_replication.sh || { echo FAIL; exit 1; }

echo PASS
