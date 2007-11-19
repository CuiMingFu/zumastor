#!/bin/bash

. config_uml
. config_replication

ssh $SSH_OPTS $source_uml_host "zumastor forget volume $vol"
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol"

ssh $SSH_OPTS $source_uml_host "halt"
ssh $SSH_OPTS $target_uml_host "halt"
