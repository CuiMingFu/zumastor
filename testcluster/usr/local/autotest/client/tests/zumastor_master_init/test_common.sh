#!/bin/sh

bootdisk() {
  cat /proc/partitions |grep -v -e blocks -e '^$' -e '[0-9]$'|tr -s ' '|cut -d' ' -f 4,5|sort -n|cut -d' ' -f2|head -n1
}
testdisks() {
  bootdisk=`bootdisk`
  cat /proc/partitions |grep -v -e blocks -e '^$' -e '[0-9]$'|tr -s ' '|cut -d' ' -f 4,5|sort -n|cut -d' ' -f2|grep -v $bootdisk
}
removemd() {
  if mdadm --detail --scan|grep -q -v ^$
  then
    for array in /dev/md*
    do
      mdadm --stop $array 2>/dev/null >/dev/null
    done
  fi
  for i in `testdisks`
  do
    mdadm --zero-superblock /dev/$i 2>/dev/null >/dev/null
  done
}
removezuma() {
  for vol in `ls /var/lib/zumastor/volumes 2>/dev/null`
  do
    zumastor forget volume $vol || true
    sleep 1
    zumastor forget volume $vol || true
    sleep 1
    zumastor forget volume $vol || true
  done
}
removelvs() {
  vg=$1
  for lv in `ls /dev/$vg/* 2>/dev/null`
  do
    echo "trying to remove lv $lv from vg $vg"
    umount $lv 2>/dev/null >/dev/null || true
    lvremove -f $lv 2>/dev/null >/dev/null || true
  done
}
removepv() {
  for bd in `testdisks` `ls /dev|grep 'md[0-9]'`
  do
    bd=/dev/$bd
    vg=`pvdisplay $bd|grep 'VG Name'|awk '{print $3}'` 2>/dev/null
    if [ -n "$vg" ]
    then
      removelvs $vg
    fi
    pvremove -ff -y $bd 2>/dev/null >/dev/null || true
  done
}
fourdiskconfig() {
  metadisk1=`testdisks|head -n1`
  metadisk2=`testdisks|grep -v $metadisk1|head -n1`
  snapdisk=`testdisks|grep -v -e $metadisk1 -e $metadisk2|head -n1`
  origindisk=`testdisks|grep -v -e $metadisk1 -e $metadisk2 -e $snapdisk|head -n1`
  echo "meta $metadisk1 $metadisk2"
  echo "snap $snapdisk"
  echo "origin $origindisk"
  mdadm --create --level 0 --raid-disks 2 /dev/md0 \
       /dev/$metadisk1 /dev/$metadisk2
  pvcreate /dev/md0
  vgcreate metavg /dev/md0
  pvcreate /dev/$snapdisk
  vgcreate snapvg /dev/$snapdisk
  pvcreate /dev/$origindisk
  vgcreate originvg /dev/$origindisk
}
fivediskconfig() {
  metadisk1=`testdisks|head -n1`
  metadisk2=`testdisks|grep -v $metadisk1|head -n1`
  snapdisk=`testdisks|grep -v -e $metadisk1 -e $metadisk2|head -n1`
  origindisk1=`testdisks|grep -v -e $metadisk1 -e $metadisk2 -e $snapdisk|head -n1`
  origindisk2=`testdisks|grep -v -e $metadisk1 -e $metadisk2 -e $snapdisk -e $origindisk1|head -n1`
  echo "meta $metadisk1 $metadisk2"
  echo "snap $snapdisk"
  echo "origin $origindisk1 $origindisk2"
  mdadm --create --level 0 --raid-disks 2 /dev/md0 \
       /dev/$metadisk1 /dev/$metadisk2
  mdadm --create --level 0 --raid-disks 2 /dev/md1 \
       /dev/$origindisk1 /dev/$origindisk2
  pvcreate /dev/md0
  vgcreate metavg /dev/md0
  pvcreate /dev/$snapdisk
  vgcreate snapvg /dev/$snapdisk
  pvcreate /dev/md1
  vgcreate originvg /dev/md1
}
baddiskconfig() {
  dd if=/dev/zero of=/var/tmp/1.img bs=1G seek=500
  dd if=/dev/zero of=/var/tmp/2.img bs=1G seek=500
  dd if=/dev/zero of=/var/tmp/3.img bs=1G seek=500
  losetup /dev/loop0 /var/tmp/1.img
  losetup /dev/loop1 /var/tmp/2.img
  losetup /dev/loop2 /var/tmp/3.img
  pvcreate /dev/loop0
  vgcreate metavg /dev/loop0
  pvcreate /dev/loop1
  vgcreate snapvg /dev/loop1
  pvcreate /dev/loop2
  vgcreate originvg /dev/loop2
}
pickdiskconfig() {
  testdevcount=`testdisks|wc -l`

  if [ $testdevcount -eq 4 ]
  then
    fourdiskconfig
  elif [ $testdevcount -eq 5 ]
  then
    fivediskconfig
  else
    baddiskconfig
  fi
}
createzumastorsetup() {
  lvcreate -n origin -L400G originvg
  lvcreate -n snap -L400G snapvg
  lvcreate -n meta -L400G metavg
  zumastor define volume testvol /dev/originvg/origin /dev/snapvg/snap -i \
           -m /dev/metavg/meta -k 256M
  mkfs.ext3 /dev/mapper/testvol
}
