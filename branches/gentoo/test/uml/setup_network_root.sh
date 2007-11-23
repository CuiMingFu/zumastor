#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# configure uml network

[[ $# -eq 3 ]] || [[ $# -eq 4 ]] || { echo "Usage: $setup_network.sh uml_ip uml_hostname [uml_fs]"; exit 1; }
uml_ip=$1
host_tap_ip=$2
uml_host=$3
uml_fs=$4

echo -n Setting up IP MASQUERADE...
iptables -t nat -L | grep "MASQUERADE" >& /dev/null || iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE || { echo "please check and set the following kernel config options"; echo "Networking -> Network Options -> Network packet filtering framework -> Core Netfilter Configuration -> Netfilter connection tracking support"; echo "Networking -> Network Options -> Network packet filtering framework -> IP: Netfilter Configuration -> IPv4 connection tracking support && Full NAT && MASQUERADE target support"; exit $?; }
echo -e "done.\n"

echo -n Setting up host networks...
grep $uml_host /etc/hosts || echo "$uml_ip $uml_host" >> /etc/hosts
echo -e "done.\n"

if [[ ! -z $uml_fs ]]; then
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
	cp /etc/resolv.conf /mnt/etc/resolv.conf
	echo $uml_host > /mnt/etc/hostname
	umount /mnt
	echo -e "done.\n"
fi
