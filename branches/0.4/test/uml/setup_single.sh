#!/bin/bash

. config_uml
. config_single

function create_device {
        dd if=/dev/zero of=$1 count=102400 bs=1024 >& /dev/null
        [[ $? -eq 0 ]] || { echo "can not create device $1, error $?"; exit -1; }
}

# build linux uml kernel
[[ -e linux-${KERNEL_VERSION} ]] || . build_uml.sh

# setup source and target file system and network
initial=1
[[ -e $uml_fs ]] && initial=0 || . build_fs.sh $uml_fs

echo -n Setting up IP MASQUERADE...
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE >> $LOG || { echo "please check and set the following kernel config options"; echo "Networking -> Network Options -> Network packet filtering framework -> Core Netfilter Configuration -> Netfilter connection tracking support"; echo "Networking -> Network Options -> Network packet filtering framework -> IP: Netfilter Configuration -> IPv4 connection tracking support && Full NAT && MASQUERADE target support"; exit $?; }
echo -e "done.\n"

[[ $initial -eq 1 ]] && . setup_network.sh $uml_ip $uml_host $uml_fs

[[ -e $ubdb_dev ]] || create_device $ubdb_dev
[[ -e $ubdc_dev ]] || create_device $ubdc_dev

# we need to do something special for relative paths
[[ $ubdb_dev == /* ]] || ubdb_dev=../$ubdb_dev
[[ $ubdc_dev == /* ]] || ubdc_dev=../$ubdc_dev

# load uml
echo -n Bring up uml...
cd linux-${KERNEL_VERSION}
screen -d -m ./linux ubda=../$uml_fs ubdb=$ubdb_dev ubdc=$ubdc_dev eth0=tuntap,,,$host_tap_ip mem=64M umid=$uml_host
cd ..
echo -e "done.\n"
sleep 30

# It could take a while for the machine to get ready
ssh $SSH_OPTS_ready=false
for i in `seq 10`; do
  if ssh $SSH_OPTS $SSH_OPTS $source_uml_host /bin/true; then
    ssh $SSH_OPTS_ready=true
    break
  fi
  sleep 20
done

if ! $ssh $SSH_OPTS_ready; then
  echo "Couldn't connect to the source uml"
  exit 1
fi

echo -n Setting up volume...
ssh $SSH_OPTS $uml_host "/etc/init.d/zumastor start"
ssh $SSH_OPTS $uml_host "zumastor forget volume $vol"
ssh $SSH_OPTS $uml_host "echo y | zumastor define volume -i $vol /dev/ubdb /dev/ubdc"
ssh $SSH_OPTS $uml_host "mkfs.ext3 /dev/mapper/$vol"
echo -e "done.\n"
