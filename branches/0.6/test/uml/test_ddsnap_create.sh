#!/bin/bash

. config_uml
. config_single

CHUNKSIZE=16k

# test low-level ddsnap creation/deletion
./start_uml.sh $uml_fs $ubdb_dev $ubdc_dev $uml_host || { echo UNRESOLVED; exit 1; }

echo -n Start ddsnap agent and server...
ssh $SSH_OPTS $uml_host "ddsnap initialize -y -c $CHUNKSIZE /dev/ubdc /dev/ubdb" 
ssh $SSH_OPTS $uml_host "ddsnap agent --logfile /tmp/srcagt.log /tmp/src.control"
ssh $SSH_OPTS $uml_host "ddsnap server --logfile /tmp/srcsvr.log /dev/ubdc /dev/ubdb /tmp/src.control /tmp/src.server"
echo -e "done.\n"

echo -n Create origin device...
size=$(ssh $SSH_OPTS $uml_host "ddsnap status /tmp/src.server --size") || echo FAIL
ssh $SSH_OPTS $uml_host "echo 0 $size ddsnap /dev/ubdc /dev/ubdb /tmp/src.control -1 | dmsetup create $vol"
[[ $? -ne 0 ]] && { echo FAIL; exit 1; }
echo -e "done.\n"

count=0
while [[ $count -lt $ITERATIONS ]]; do
	echo count $count
	ssh $SSH_OPTS $uml_host "ddsnap create /tmp/src.server $count"
	ssh $SSH_OPTS $uml_host "echo 0 $size ddsnap /dev/ubdc /dev/ubdb /tmp/src.control $count | dmsetup create $vol\($count\)"
	error=$?
	if [[ $error -ne 0 ]]; then
		echo FAIL
		exit 1
	else
		sleep 10
		ssh $SSH_OPTS $uml_host "dmsetup remove $vol\($count\)"
		ssh $SSH_OPTS $uml_host "ddsnap delete /tmp/src.server $count"
		count=$(( count + 2 ))
	fi
done

ssh $SSH_OPTS $uml_host "zumastor forget volume $vol" >& $LOG
./stop_uml.sh $uml_host $uml_fs || { echo FAIL; exit 1; }
echo PASS
