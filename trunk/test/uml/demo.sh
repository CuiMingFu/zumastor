#!/bin/sh
# Demostrates how to run all UML-based Zumastor tests
set -e
set -x

# One-time setup common to all tests
. config_uml
test -e  linux-${KERNEL_VERSION}/linux || sh build_uml.sh
test -e uml_fs1 || { sh build_fs.sh uml_fs1; sudo sh build_fs_root.sh uml_fs1; }

# Just run each test once through, for a quick sanity check
ITERATIONS=1
export ITERATIONS

# Single node tests
sudo sh setup_network_root.sh 192.168.100.1 192.168.100.111 uml_1 uml_fs1
. config_single
sh test_agent_die.sh

# Two-node tests
cp uml_fs1 uml_fs2
chmod a+rw uml_fs2
sudo sh setup_network_root.sh 192.168.100.2 192.168.100.111 uml_2 uml_fs2
. config_replication
sh test_fullreplication.sh
sh test_source_stop.sh
sh test_target_stop.sh

echo All tests complete.
