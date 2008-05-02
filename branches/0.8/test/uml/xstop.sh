#!/bin/bash
# Example of how to shut down the UML instance started by login.sh
. ./config_uml
. ./config_single
set -ex
./stop_uml.sh  $uml_host $uml_fs
