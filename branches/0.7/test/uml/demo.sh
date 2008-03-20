#!/bin/sh
# Demostrates how to run all UML-based Zumastor tests
set -e
set -x

# utilities to build debian packages and run uml
dpkg -s build-essential devscripts fakeroot debhelper > /dev/null 2>&1 || sudo apt-get install build-essential devscripts fakeroot debhelper
dpkg -s uml-utilities > /dev/null 2>&1 || sudo apt-get install uml-utilities
[[ -x /usr/lib/uml/uml_net ]] || sudo chmod a+rx /usr/lib/uml/uml_net  # allow normal user to start uml network

# One-time setup common to all tests
SRCDIR="${PWD}"
. ./config_uml
./download.sh
mkdir -p $WORKDIR
test -e  $WORKDIR/linux-${KERNEL_VERSION}/linux || ./build_uml.sh
cp build_fs_root.sh setup_network_root.sh $WORKDIR/
chmod a+rx $WORKDIR $WORKDIR/build_fs_root.sh $WORKDIR/setup_network_root.sh
test -e $WORKDIR/uml_fs1 || { ./build_fs.sh uml_fs1; cd $WORKDIR; sudo ./build_fs_root.sh uml_fs1; cd $SRCDIR; }
rm -f $WORKDIR/build_fs_root.sh

# Just run each test once through, for a quick sanity check
ITERATIONS=1
export ITERATIONS

# Single node tests
cd $WORKDIR
sudo ./setup_network_root.sh 192.168.100.1 192.168.100.111 uml_1 uml_fs1
cd $SRCDIR
. ./config_single
./test_ddsnap_create.sh
./test_agent_die.sh

# Two-node tests
cd $WORKDIR
test -e uml_fs2 || { cp uml_fs1 uml_fs2; chmod a+rw uml_fs2; }
sudo ./setup_network_root.sh 192.168.100.2 192.168.100.111 uml_2 uml_fs2
cd $SRCDIR
rm -f $WORKDIR/setup_network_root.sh
. ./config_replication
./test_fullreplication.sh
./test_source_stop.sh
./test_target_stop.sh

echo All tests complete.
