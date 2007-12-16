#!/bin/bash
# Example of how to start one UML instance (if needed) and copy a single file from it
. ./config_uml
. ./config_single
ps augxw | grep linux | grep -q -v grep || ./start_uml.sh $uml_fs $ubdb_dev $ubdc_dev $uml_host
set -ex
scp $SCP_OPTS root@${uml_host}:"$1" .
