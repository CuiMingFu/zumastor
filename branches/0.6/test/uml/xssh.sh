#!/bin/bash
# Example of how to start one UML instance (if needed) and log into it
# (or run a command remotely)
. ./config_uml
. ./config_single
ps augxw | grep linux | grep -q -v grep || ./start_uml.sh $uml_fs $ubdb_dev $ubdc_dev $uml_host
set -ex
ssh $SSH_OPTS $uml_host "$@"
