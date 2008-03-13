#!/bin/sh -x
# Setup an ubuntu instance under openvz
# Gets the ubuntu cache file, creates a network between host and guest, and
#  gives the guest 512M memory privs
# Author: Will Nowak <wan@ccs.neu.edu

VMID=102
HOST_NAME=`hostname|cut -d'.' -f 1`

if [ ! -f /var/lib/vz/template/cache/ubuntu-7.10-i386-minimal.tar.gz ]
then
  sudo wget http://download.openvz.org/template/precreated/ubuntu-7.10-i386-minimal.tar.gz -O /var/lib/vz/template/cache/ubuntu-7.10-i386-minimal.tar.gz
fi

if [ -d /var/lib/vz/private/$VMID/ ]
then
  sudo vzctl stop $VMID
  sudo vzctl destroy $VMID
fi

VIRT_HOST_ETH=`./easymac.sh -R -p`
VIRT_VM_ETH=`./easymac.sh -R -p`

sudo vzctl create $VMID --ostemplate ubuntu-7.10-i386-minimal \
                      --hostname "${HOST_NAME}-vz${VMID}"
sudo vzctl set $VMID --meminfo privvmpages:8 --save
sudo vzctl set $VMID --netif_add eth0,$VIRT_VM_ETH,vm${VMID},$VIRT_HOST_ETH \
                   --save

sudo vzctl start $VMID
sudo ifconfig vm${VMID} 192.168.1.1 netmask 255.255.255.248 \
                                    broadcast 192.168.1.7
sudo vzctl exec $VMID ifconfig eth0 192.168.1.2 netmask 255.255.255.248 \
                                                broadcast 192.168.1.7
