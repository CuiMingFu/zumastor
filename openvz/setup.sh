#!/bin/sh -x
#
# $Id$
#
# create an origin and snapshot device with filesystem for use
# by an OpenVZ VE.
# VE number, LVM vg name, and sizes of origin and snapshot stores (with M,G,
# or T appended) to be passed.
#
# Neither zumastor nor OpenVZ require LVM, but it's convenient.  Rewrite
# if you want to use another source of on-demand block devices.
#
# Copyright 2008 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

ve=$1
vgname=$2
originsize=$3
snapsize=$4

debarchive=ftp.us.debian.org

if ! which lvcreate ; then
  echo "Install lvm2 utilities"
  exit 1
fi

if ! which ddsnap ; then
  echo "Install the zumastor ddsnap utility.  cd zumastor-0.6 to find the deb"
  exit 1
fi
 
if [ ! -d /dev/$vgname ] ; then
  cat <<EOF
Create LVM volume group $vgname first.  eg.
pvcreate -ff /dev/sdb6
vgcreate $vgname /dev/sdb6
fi
EOF
  exit 1
fi

# create a directory for socket control of ddsnap
sockdir=`mktemp -d ve${ve}zuma.XXXX`
echo "socket directory: $sockdir"

# create the backing block devices for the ddsnap volume
lvcreate --size $originsize -n ${ve}origin $vgname
lvcreate --size $snapsize -n ${ve}snap $vgname
origin=/dev/$vgname/${ve}origin
snap=/dev/$vgname/${ve}snap

# initialize the snapstore with the basic ddsnap bitmaps and metadata
# start the control and server daemons
ddsnap initialize $snap $origin
control=$sockdir/control
ddsnap agent $control
$server=$sockdir/server
ddsnap server $snap $origin $control $server

# get the size and use dmsetup to map the origin device using dmsetup
size=`ddsnap status $server --size`
echo 0 $size ddsnap $snap $origin $control -1 | \
  dmsetup create ${ve}vol

# create a filesystem on the new ddsnap block device and mount
# where OpenVZ expects it
mkfs.ext3 /dev/mapper/${ve}vol
VZ=''
for vz in /var/lib/vz /vz ; do
  if [ -d $vz ] ; then
    VZ=$vz
  fi
done
if [ ! -d $VZ/private/$ve ] ; then
  echo "VE $ve not defined"
  exit 1
fi
mount /dev/mapper/${ve}vol $VZ/private/$ve

# create an etch instance using debootstrap if available
if which debootstrap ; then
  arch=i386
  if which dpkg ; then arch=`dpkg --print-architecture` ; fi
  debootstrap --arch $arch etch /var/lib/vz/private/$VE http://$debarchive/debian/
fi

#
# how snapshot #0 would be created and turned into a block device
#
# num=0
# ddsnap create $server $num
# echo 0 $size ddsnap $snap $origin $control $num | dmsetup create ${ve}snap-$num

 # rmdir $sockdir


# snapshots are writable, so multiple snapshots may be reused to share space
# among multiple installs, as well as to calculate and apply deltas
# during a migration
