#!/bin/sh
sorteddisks=`grep -v -e ^$ -e name$ -e [0-9]$ /proc/partitions|tr -s " "|cut -d" " -f4,5|sort -n|cut -d" " -f2`
bootdisk=`echo $sorteddisks|head -n1`
#debconf-set partman-auto/disk /dev/$bootdisk

for disk in $sorteddisks
do
  echo $disk
  #dd if=/dev/zero of=/dev/$disk bs=1M count=100
done
