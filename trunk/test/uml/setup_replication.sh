#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

. config_uml
. config_replication

sh start_uml.sh $source_uml_fs $source_ubdb_dev $source_ubdc_dev $source_uml_host
sh start_uml.sh $target_uml_fs $target_ubdb_dev $target_ubdc_dev $target_uml_host

# set up ssh keys for source and target umls to access each other
ssh $SSH_OPTS $source_uml_host "ssh $SSH_OPTS $target_uml_host 'echo'" >& /dev/null
status=$?
if [[ $status -ne 0 ]]; then
	echo -n Setting up ssh-keygen...
	ssh $SSH_OPTS $source_uml_host "rm /root/.ssh/id_dsa; rm /root/.ssh/id_dsa.pub" >& $LOG
	ssh $SSH_OPTS $source_uml_host "ssh-keygen -t dsa -f /root/.ssh/id_dsa -P ''" >& $LOG
	scp $SCP_OPTS root@$source_uml_host:/root/.ssh/id_dsa.pub /tmp/$source_uml_host.pub >& $LOG
	scp $SCP_OPTS /tmp/$source_uml_host.pub root@$target_uml_host:/root/.ssh/$source_uml_host.pub
	rm -f /tmp/$source_uml_host.pub
	ssh $SSH_OPTS $target_uml_host "cat /root/.ssh/$source_uml_host.pub >> /root/.ssh/authorized_keys"
	ssh $SSH_OPTS $source_uml_host "echo $target_uml_ip $target_uml_host > /etc/hosts"

	ssh $SSH_OPTS $target_uml_host "rm /root/.ssh/id_dsa; rm /root/.ssh/id_dsa.pub" >& $LOG
	ssh $SSH_OPTS $target_uml_host "ssh-keygen -t dsa -f /root/.ssh/id_dsa -P ''" >& $LOG
	scp $SCP_OPTS root@$target_uml_host:/root/.ssh/id_dsa.pub /tmp/$target_uml_host.pub
	scp $SCP_OPTS /tmp/$target_uml_host.pub root@$source_uml_host:/root/.ssh/$target_uml_host.pub
	rm -f /tmp/$target_uml_host.pub
	ssh $SSH_OPTS $source_uml_host "cat /root/.ssh/$target_uml_host.pub >> /root/.ssh/authorized_keys"
	ssh $SSH_OPTS $target_uml_host "echo $source_uml_ip $source_uml_host > /etc/hosts"
	echo -e "done.\n"
fi

# set up source and target volumes according to the configuratiosn in config_replication
echo -n Setting up source volume...
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $source_uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc" >& $LOG
ssh $SSH_OPTS $source_uml_host "mkfs.ext3 /dev/mapper/$vol" >& $LOG
ssh $SSH_OPTS $source_uml_host "zumastor define master $vol -h $hourly_snapnum -d 7" >& $LOG
echo -e "done.\n"

echo -n Setting up target volume...
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $target_uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc" >& $LOG
echo -e "done.\n"

