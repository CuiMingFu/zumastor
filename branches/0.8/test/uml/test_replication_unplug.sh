#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# test replication resuming after network goes down and up
# this program requires root previlige

. config_uml
. config_replication

./start_replication.sh || { echo UNRESOLVED; exit 1; }

echo -n Start replication ...
ssh $SSH_OPTS $source_uml_host "zumastor define target $vol $target_uml_host:$target_port -p 1" >& $LOG || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "zumastor define source $vol $source_uml_host -p $interval" >& $LOG || { echo FAIL; exit 1; }
echo -e "done.\n"
sleep 30

up_time=60
DOWNMAX=100

last_mod_time=$(ssh $SSH_OPTS $source_uml_host stat -c %Y $VOLUMES/$vol/targets/$target_uml_host)
count=0
noprogress_count=0
while [[ $count -lt $ITERATIONS ]]; do
        while true; do
        	sleep $up_time
		mod_time=$(ssh $SSH_OPTS $source_uml_host stat -c %Y $VOLUMES/$vol/targets/$target_uml_host/send)
        	[[ $? -ne 0 ]] && mod_time=$(ssh $SSH_OPTS $source_uml_host stat -c %Y $VOLUMES/$vol/targets/$target_uml_host)
        	if [[ $mod_time -eq $last_mod_time ]]; then
                	echo "no progress after network resuming, $mod_time $last_mod_time"
                	ssh $SSH_OPTS $source_uml_host "cat $VOLUMES/$vol/targets/$target_uml_host/send"
                	ssh $SSH_OPTS $source_uml_host "ps axo stat,comm,pid,pgid,comm,etime,wchan | grep ssh"
			noprogress_count=$(( noprogress_count+1 ))
			[[ $noprogress_count -lt 5 ]] || { echo FAIL; exit 1; }
        	else
			noprogress_count=0
                	break
        	fi
        done
        last_mod_time=$mod_time
	down_time=$(( RANDOM * DOWNMAX / 32768 ))
        echo unplug $down_time seconds
	echo 0 > /proc/sys/net/ipv4/ip_forward
        sleep $down_time
	echo resume
	echo 1 > /proc/sys/net/ipv4/ip_forward
	count=$(( count+1 ))
done

./stop_replication.sh || { echo FAIL; exit 1; }

echo PASS
