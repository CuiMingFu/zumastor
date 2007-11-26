#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# shutdown source and target umls
. config_uml
. config_replication

ssh $SSH_OPTS $source_uml_host "zumastor forget volume $vol" >& /dev/null
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol" >& /dev/null

./stop_uml.sh $source_uml_host $source_uml_fs || exit 1
./stop_uml.sh $target_uml_host $target_uml_fs || exit 1

exit 0
