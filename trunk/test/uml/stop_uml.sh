#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# shutdown a uml and wait for its completion

. config_uml

[[ $# -eq 2 ]] || { echo "Usage: stop_uml.sh uml_host uml_fs"; exit 1; }

uml_host=$1
uml_fs=$2
ssh $SSH_OPTS $uml_host "halt" >& /dev/null || { echo FAIL; exit 1; }
for i in `seq 10`; do
	pgrep -f "./linux ubda=../$uml_fs" >& /dev/null || exit 0
	sleep 5
done
exit 1
