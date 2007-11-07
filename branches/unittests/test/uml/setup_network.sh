#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# configure uml network

. config_uml

[[ $# -eq 3 ]] || { echo "Usage: $setup_network.sh uml_ip uml_hostname uml_fs"; exit 1; }
uml_ip=$1
uml_host=$2
uml_fs=$3

echo -n Setting up host networks...
grep $uml_host /etc/hosts >> $LOG || echo "$uml_ip $uml_host" >> /etc/hosts
echo -e "done.\n"

echo -n Setting up virtual network...
mount -o loop $uml_fs /mnt || exit 1
echo "auto lo eth0" > /mnt/etc/network/interfaces
echo "" >> /mnt/etc/network/interfaces
echo "iface lo inet loopback" >> /mnt/etc/network/interfaces
echo "" >> /mnt/etc/network/interfaces
echo "iface eth0 inet static" >> /mnt/etc/network/interfaces
echo "address $uml_ip" >> /mnt/etc/network/interfaces
echo "netmask 255.255.255.0" >> /mnt/etc/network/interfaces
echo "gateway $host_tap_ip" >> /mnt/etc/network/interfaces
cp /etc/resolv.conf /mnt/etc/resolv.conf || exit 1
echo $uml_host > /mnt/etc/hostname
umount /mnt
echo -e "done.\n"
