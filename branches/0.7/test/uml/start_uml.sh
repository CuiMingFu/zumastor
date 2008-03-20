#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# load up a uml linux

. config_uml

[[ $# -eq 4 ]] || { echo "Usage: start_uml.sh uml_fs ubd_dev ubc_dev uml_host"; exit 1; }

uml_fs=$1
ubdb_dev=$2
ubdc_dev=$3
uml_host=$4

function create_device {
	fsize=`stat -c %s $1 2> /dev/null`
	if [[ -z $fsize || $fsize -lt 104857600 ]]; then
        	[[  $1 == /dev/sysvg/* ]] || dd if=/dev/zero of=$1 count=102400 bs=1024 >& /dev/null
        	[[ $? -eq 0 ]] || { echo "can not create device $1, error $?"; exit -1; }
	fi
}

pushd $WORKDIR
[[ -e $ubdb_dev ]] || create_device $ubdb_dev
[[ -e $ubdc_dev ]] || create_device $ubdc_dev

# we need to do something special for relative paths
[[ $ubdb_dev == /* ]] || ubdb_dev=../$ubdb_dev
[[ $ubdc_dev == /* ]] || ubdc_dev=../$ubdc_dev

# killing any running linux umls with the same root file system image
pkill -f "./linux ubda=../$uml_fs" >& /dev/null
# load uml. uml does not work properly when running in background, so use screen detach here.
echo -n Bring up uml...
cd linux-${KERNEL_VERSION}
screen -d -m ./linux ubda=../$uml_fs ubdb=$ubdb_dev ubdc=$ubdc_dev eth0=tuntap,,,$host_tap_ip mem=64M umid=$uml_host
cd ..
sleep 10
popd

# It could take a while for the machine to get ready
ssh_ready=false
for i in `seq 10`; do
  if ssh $SSH_OPTS $uml_host /bin/true; then
    ssh_ready=true
    break
  fi
  sleep 20
done
echo -e "done.\n"

if ! $ssh $SSH_OPTS_ready; then
  echo "Couldn't connect to the source uml"
  exit 1
fi
