#!/bin/bash

. config_uml
. config_single

function create_device {
        dd if=/dev/zero of=$1 count=102400 bs=1024 >& /dev/null
        [[ $? -eq 0 ]] || { echo "can not create device $1, error $?"; exit -1; }
}

# build linux uml kernel
[[ -e linux-${KERNEL_VERSION} ]] || ./build_uml.sh

# setup uml file system and network
initial=
[[ -e $uml_fs ]] || { initial=$uml_fs; ./build_fs.sh $uml_fs; }

[[ $USER == "root" ]] && ./setup_network_root.sh $uml_ip $host_tap_ip $uml_host $initial

[[ -e $ubdb_dev ]] || create_device $ubdb_dev
[[ -e $ubdc_dev ]] || create_device $ubdc_dev

# we need to do something special for relative paths
[[ $ubdb_dev == /* ]] || ubdb_dev=../$ubdb_dev
[[ $ubdc_dev == /* ]] || ubdc_dev=../$ubdc_dev

# load uml. uml does not work properly when running in background, so use screen detach here.
echo -n Bring up uml...
cd linux-${KERNEL_VERSION}
screen -d -m ./linux ubda=../$uml_fs ubdb=$ubdb_dev ubdc=$ubdc_dev eth0=tuntap,,,$host_tap_ip mem=64M umid=$uml_host
cd ..
echo -e "done.\n"
sleep 30

# It could take a while for the machine to get ready
ssh_ready=false
for i in `seq 10`; do
  if ssh $SSH_OPTS $uml_host /bin/true; then
    ssh_ready=true
    break
  fi
  sleep 20
done

if ! $ssh $SSH_OPTS_ready; then
  echo "Couldn't connect to the source uml"
  exit 1
fi

echo -n Setting up volume...
ssh $SSH_OPTS $uml_host "zumastor forget volume $vol"
ssh $SSH_OPTS $uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc"
ssh $SSH_OPTS $uml_host "mkfs.ext3 /dev/mapper/$vol"
echo -e "done.\n"
