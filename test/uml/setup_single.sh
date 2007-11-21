#!/bin/bash

. config_uml
. config_single

sh start_uml.sh $uml_fs $ubdb_dev $ubdc_dev $uml_host

echo -n Setting up volume...
ssh $SSH_OPTS $uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc" >& $LOG
ssh $SSH_OPTS $uml_host "mkfs.ext3 /dev/mapper/$vol" >& $LOG
echo -e "done.\n"
