#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

. config_uml
. config_replication

function create_device {
        dd if=/dev/zero of=$1 count=102400 bs=1024 >& /dev/null
        [[ $? -eq 0 ]] || { echo "can not create device $1, error $?"; exit -1; }
}

# build linux uml kernel
[[ -e linux-${KERNEL_VERSION} ]] || ./build_uml.sh

# setup source and target file system and network
# if source_fs and target_fs both exist, do nothing assuming everything is setup
# if only source_fs exists (e.g., a copy from a previously configured image), 
#     re-config network and copy it to target_fs
# if source_fs does not exist, download the Debian image and build everything needed
source_initial=$source_uml_fs
target_initial=$target_uml_fs 
[[ -e $source_uml_fs ]] && [[ -e $target_uml_fs ]] && source_initial= && target_initial=
[[ -e $source_uml_fs ]] || ./build_fs.sh $source_uml_fs
[[ -e $target_uml_fs ]] || cp $source_uml_fs $target_uml_fs

[[ $USER == "root" ]] && ./setup_network_root.sh $source_uml_ip $host_tap_ip $source_uml_host $source_initial
[[ $USER == "root" ]] && ./setup_network_root.sh $target_uml_ip $host_tap_ip $target_uml_host $target_initial

[[ -e $source_ubdb_dev ]] || create_device $source_ubdb_dev
[[ -e $source_ubdc_dev ]] || create_device $source_ubdc_dev
[[ -e $target_ubdb_dev ]] || create_device $target_ubdb_dev
[[ -e $target_ubdc_dev ]] || create_device $target_ubdc_dev

# we need to do something special for relative paths
[[ $source_ubdb_dev == /* ]] || source_ubdb_dev=../$source_ubdb_dev
[[ $source_ubdc_dev == /* ]] || source_ubdc_dev=../$source_ubdc_dev
[[ $target_ubdb_dev == /* ]] || target_ubdb_dev=../$target_ubdb_dev
[[ $target_ubdc_dev == /* ]] || target_ubdc_dev=../$target_ubdc_dev

# load source and target
echo -n Bring up source uml...
cd linux-${KERNEL_VERSION}
screen -d -m ./linux ubda=../$source_uml_fs ubdb=$source_ubdb_dev ubdc=$source_ubdc_dev eth0=tuntap,,,$host_tap_ip mem=64M umid=$source_uml_host
cd ..
echo -e "done.\n"
sleep 30

echo -n Bring up target uml...
cd linux-${KERNEL_VERSION}
screen -d -m ./linux ubda=../$target_uml_fs ubdb=$target_ubdb_dev ubdc=$target_ubdc_dev eth0=tuntap,,,$host_tap_ip mem=64M umid=$target_uml_host
cd ..
echo -e "done.\n"
sleep 30

# It could take a while for the machine to get ready
ssh_ready=false
for i in `seq 10`; do
  if ssh $SSH_OPTS $source_uml_host /bin/true; then
    ssh_ready=true
    break
  fi
  sleep 20
done

if ! $ssh_ready; then
  echo "Couldn't connect to the source uml"
  exit 1
fi

# set up ssh keys for source and target umls to access each other
ssh $SSH_OPTS $source_uml_host "ssh $SSH_OPTS $target_uml_host 'echo'" >& /dev/null
status=$?
if [[ $status -ne 0 ]]; then
	echo -n Setting up ssh-keygen...
	ssh $SSH_OPTS $source_uml_host "rm /root/.ssh/id_dsa; rm /root/.ssh/id_dsa.pub" >& $LOG
	ssh $SSH_OPTS $source_uml_host "ssh-keygen -t dsa -f /root/.ssh/id_dsa -P ''" >& $LOG
	scp $SCP_OPTS root@$source_uml_host:/root/.ssh/id_dsa.pub /tmp/$source_uml_host.pub >& $LOG
	scp $SCP_OPTS /tmp/$source_uml_host.pub root@$target_uml_host:/root/.ssh/$source_uml_host.pub
	rm -f /tmp/$source_uml_host.pub
	ssh $SSH_OPTS $target_uml_host "cat /root/.ssh/$source_uml_host.pub >> /root/.ssh/authorized_keys"
	ssh $SSH_OPTS $source_uml_host "echo $target_uml_ip $target_uml_host > /etc/hosts"

	ssh $SSH_OPTS $target_uml_host "rm /root/.ssh/id_dsa; rm /root/.ssh/id_dsa.pub" >& $LOG
	ssh $SSH_OPTS $target_uml_host "ssh-keygen -t dsa -f /root/.ssh/id_dsa -P ''" >& $LOG
	scp $SCP_OPTS root@$target_uml_host:/root/.ssh/id_dsa.pub /tmp/$target_uml_host.pub
	scp $SCP_OPTS /tmp/$target_uml_host.pub root@$source_uml_host:/root/.ssh/$target_uml_host.pub
	rm -f /tmp/$target_uml_host.pub
	ssh $SSH_OPTS $source_uml_host "cat /root/.ssh/$target_uml_host.pub >> /root/.ssh/authorized_keys"
	ssh $SSH_OPTS $target_uml_host "echo $source_uml_ip $source_uml_host > /etc/hosts"
	echo -e "done.\n"
fi

# set up source and target volumes according to the configuratiosn in config_replication
echo -n Setting up source volume...
ssh $SSH_OPTS $source_uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $source_uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc" >& $LOG
ssh $SSH_OPTS $source_uml_host "mkfs.ext3 /dev/mapper/$vol" >& $LOG
ssh $SSH_OPTS $source_uml_host "zumastor define master $vol -h $hourly_snapnum -d 7" >& $LOG
echo -e "done.\n"

echo -n Setting up target volume...
ssh $SSH_OPTS $target_uml_host "zumastor forget volume $vol" >& $LOG
ssh $SSH_OPTS $target_uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc" >& $LOG
echo -e "done.\n"

