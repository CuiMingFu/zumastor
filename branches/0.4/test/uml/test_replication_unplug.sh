#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# test replication resuming after network goes down and up

. config_replication

. setup_replication.sh || { echo UNRESOLVED; exit 1; }

echo -n Start replication ...
ssh $SSH_OPTS $source_uml_host "zumastor define target $vol $target_uml_host:$target_port -p 1" || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "zumastor define source $vol $source_uml_host -p $interval" || { echo FAIL; exit 1; }
echo -e "done.\n"
sleep 30

up_time=60
DOWN_TIME="10 20 30 40 50 60 120 180 300 600 900 1200 1800 2400"

last_mod_time=$(ssh $SSH_OPTS $source_uml_host stat -c %Y $VOLUMES/$vol/targets/$target_uml_host)
noprogress_count=0
for down_time in $DOWN_TIME; do
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
        echo unplug $down_time seconds
	echo 0 > /proc/sys/net/ipv4/ip_forward
        sleep $down_time
	echo resume
	echo 1 > /proc/sys/net/ipv4/ip_forward
done

ssh $SSH_OPTS $source_uml_host "zumastor forget volume $vol" || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol" || { echo FAIL; exit 1; }

ssh $SSH_OPTS $source_uml_host "halt" || { echo FAIL; exit 1; }
ssh $SSH_OPTS $target_uml_host "halt" || { echo FAIL; exit 1; }

echo PASS
