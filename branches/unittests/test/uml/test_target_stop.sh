#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# test zumastor stop on target

. config_replication

. setup_replication.sh || { echo UNRESOLVED; exit 1; }

echo -n Start replication ...
ssh $SSH_OPTS $source_uml_host "zumastor define target $vol $target_uml_host:$target_port -p 1" || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "zumastor define source $vol $source_uml_host -p $interval" || { echo FAIL; exit 1; }
echo -e "done.\n"
sleep 30

TOTAL=1000
last_mod_time=$(ssh $SSH_OPTS $source_uml_host stat -c %Y $VOLUMES/$vol/source)
count=0
noprogress_count=0
while [[ $count -lt $TOTAL ]]; do
	ssh $SSH_OPTS $target_uml_host "/etc/init.d/zumastor stop" || { echo FAIL; exit 1; }
	left=$(ssh $SSH_OPTS $target_uml_host "ls /dev/mapper | grep $vol")
	if [[ $left ]]; then
		echo "zumastor stop did not stop everything, left $left"
		ssh $SSH_OPTS $target_uml_host "ps -ef | grep ddsnap"
		ssh $SSH_OPTS $target_uml_host "dmsetup ls"
		exit 1
	fi
	ssh $SSH_OPTS $target_uml_host "/etc/init.d/zumastor start" || { echo FAIL; exit 1; }
	sleep 30
	mod_time=$(ssh $SSH_OPTS $target_uml_host stat -c %Y $VOLUMES/$vol/source/apply)
        [[ $? -ne 0 ]] && mod_time=$(ssh $SSH_OPTS $target_uml_host stat -c %Y $VOLUMES/$vol/source)
       	if [[ $mod_time -eq $last_mod_time ]]; then
               	echo "no progress after network resuming, $mod_time $last_mod_time"
               	ssh $SSH_OPTS $target_uml_host "cat $VOLUMES/$vol/source/apply"
               	ssh $SSH_OPTS $target_uml_host "ps axo stat,comm,pid,pgid,comm,etime,wchan | grep ddsnap"
		noprogress_count=$(( noprogress_count+1 ))
		[[ $noprogress_count -lt 5 ]] || { echo FAIL; exit 1; }
        else
		noprogress_count=0
       	fi
        last_mod_time=$mod_time
	count=$(( count+1 ))
done

ssh $SSH_OPTS $source_uml_host "zumastor forget volume $vol" || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol" || { echo FAIL; exit 1; }

ssh $SSH_OPTS $source_uml_host "halt" || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "halt" || { echo FAIL; exit 1; }

echo PASS
