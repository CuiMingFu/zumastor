#!/bin/sh
# Demostrates how to run all UML-based Zumastor tests
set -e
set -x

# utilities to build debian packages and run uml
dpkg -s devscripts fakeroot debhelper >& /dev/null || sudo apt-get install devscripts fakeroot debhelper
dpkg -s uml-utilities >& /dev/null || sudo apt-get install uml-utilities
sudo chmod a+rx /usr/lib/uml/uml_net  # allow normal user to start uml network

# One-time setup common to all tests
. config_uml
./download.sh
test -e  linux-${KERNEL_VERSION}/linux || ./build_uml.sh
test -e uml_fs1 || { ./build_fs.sh uml_fs1; sudo ./build_fs_root.sh uml_fs1; }

# Just run each test once through, for a quick sanity check
ITERATIONS=1
export ITERATIONS

# Single node tests
sudo ./setup_network_root.sh 192.168.100.1 192.168.100.111 uml_1 uml_fs1
. config_single
./test_agent_die.sh
sleep 30

# Two-node tests
test -e uml_fs2 || { cp uml_fs1 uml_fs2; chmod a+rw uml_fs2; }
sudo ./setup_network_root.sh 192.168.100.2 192.168.100.111 uml_2 uml_fs2
. config_replication
./test_fullreplication.sh
sleep 30
./test_source_stop.sh
sleep 30
./test_target_stop.sh

echo All tests complete.
