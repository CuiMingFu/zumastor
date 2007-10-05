#!/bin/sh -x

# Set up the initial Ubuntu/dapper template image, for use when duplicating
# the install to multiple server/client tests.  Makes use of tunbr and
# a presetup br1, squid, and dnsmasq.

# $Id$
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

SSH='ssh -o StrictHostKeyChecking=no'
SCP='scp -o StrictHostKeyChecking=no'

if [ "x$MACFILE" = "x" -o "x$MACADDR" = "x" -o "x$IFACE" = "x" \
     -o "x$IPADDR" = "x" ] ; then
  echo "Run this script under tunbr"
  exit 1
fi

# Remove the any existing ssh hostkey for this IPADDR since generating a
# new one
if [ -f ~/.ssh/known_hosts ] ; then
  sed -i /^${IPADDR}\ .*\$/d ~/.ssh/known_hosts
fi

# defaults, overridden by /etc/default/testenv if it exists
# diskimgdir should be local for reasonable performance
size=10G
diskimgdir=${HOME}/testenv
tftpdir=/tftpboot
qemu_img=qemu-img  # could be kvm, kqemu version, etc.
qemu_i386=qemu  # could be kvm, kqemu version, etc.
rqemu_i386=qemu  # could be kvm, kqemu version, etc.  Must be 0.9.0 to net boot.
VIRTHOST=192.168.23.1
[ -x /etc/default/testenv ] && . /etc/default/testenv

IMAGE=dapper-i386
IMAGEDIR=${diskimgdir}/${IMAGE}
if [ "x$DISKIMG" = "x" ] ; then
  diskimg=${IMAGEDIR}/hda.img
else
  # eg. DISKIMG=../../build/${IMAGE}.img
  diskimg="$DISKIMG"
fi
SERIAL=${IMAGEDIR}/serial
MONITOR=${IMAGEDIR}/monitor
VNC=${IMAGEDIR}/vnc

if [ ! -e ${IMAGEDIR} ]; then
  mkdir -p ${IMAGEDIR}
  chmod 700 ${IMAGEDIR}
fi

if [ ! -f ${diskimg} ] ; then

  # extract and repack the initrd with the desired preseed file
  tmpdir=`mktemp -d`
  mkdir ${tmpdir}/initrd
  cp dapper.cfg ${tmpdir}/initrd/preseed.cfg
  cp common.cfg ${tmpdir}/initrd/
  cp dapper-early.sh ${tmpdir}/initrd/early.sh
  cp dapper-late.sh ${tmpdir}/initrd/late.sh
  passwd=`pwgen 8 1`
  touch ${IMAGEDIR}/root
  chmod 600 ${IMAGEDIR}/root
  echo $passwd > ${IMAGEDIR}/root
  pwhash=`echo ${passwd} | mkpasswd -s --hash=md5`
  cat >>${tmpdir}/initrd/preseed.cfg <<EOF
d-i     mirror/http/hostname    string ${VIRTHOST}
d-i     passwd/root-password-crypted    password ${pwhash}
d-i     passwd/user-password-crypted    password ${pwhash}
d-i	passwd/user-fullname            string ${USER}
d-i	passwd/username                 string ${USER}
EOF

  cat ~/.ssh/*.pub > ${tmpdir}/initrd/authorized_keys

  if [ ! -d  ${tftpdir}/${USER}/ubuntu-installer/i386 ] ; then
    mkdir -p ${tftpdir}/${USER}/ubuntu-installer/i386
  fi

  fakeroot <<EOF
cd ${tmpdir}/initrd
zcat ${tftpdir}/ubuntu-installer/i386/initrd.gz | cpio -i
find . -print0 | cpio -0 -o -H newc | gzip -9 > ${tftpdir}/${USER}/ubuntu-installer/i386/initrd.gz
EOF
  chmod ugo+r ${tftpdir}/${USER}/ubuntu-installer/i386/initrd.gz
  
  ${qemu_img} create -f qcow2 ${diskimg} ${size}

  cat >${MACFILE} <<EOF
SERIAL 0 115200 0
DEFAULT server
LABEL server
	kernel ubuntu-installer/i386/linux
	append base-config/package-selection= base-config/install-language-support=false vga=normal initrd=${USER}/ubuntu-installer/i386/initrd.gz ramdisk_size=13531 root=/dev/rd/0 rw preseed/file=/preseed.cfg noapic DEBCONF_DEBUG=5 console=tty0 console=ttyS0,115200n8
PROMPT 0
TIMEOUT 1
EOF
  chmod ugo+r ${MACFILE}

  ${rqemu_i386} \
    -serial unix:${SERIAL},server,nowait \
    -monitor unix:${MONITOR},server,nowait \
    -vnc unix:${VNC} \
    -net nic,macaddr=${MACADDR} -net tap,ifname=${IFACE},script=no \
    -boot n -hda ${diskimg} -no-reboot
    
  ${qemu_i386} \
    -serial unix:${SERIAL},server,nowait \
    -monitor unix:${MONITOR},server,nowait \
    -vnc unix:${VNC} \
    -net nic,macaddr=${MACADDR} -net tap,ifname=${IFACE},script=no \
    -boot c -hda ${diskimg} -no-reboot &

  while ! ${SSH} root@${IPADDR} hostname 2>/dev/null
  do
    echo -n .
    sleep 10
  done

  # turn the swap partition into LVM2 sysvg
  ${SCP} swap2sysvg.sh root@${IPADDR}:
  ${SSH} root@${IPADDR} './swap2sysvg.sh && rm swap2sysvg.sh'

  # generate and authorize an passwordless ssh key that can log in to
  # any other image with this as its base template
  ${SSH} root@${IPADDR} "ssh-keygen -q -P '' -t dsa -f .ssh/id_dsa ; cat .ssh/id_dsa.pub >> .ssh/authorized_keys"

  ${SSH} root@${IPADDR} halt

  wait

  echo "${diskimg} installed.  root and ${USER} passwords are: ${passwd}"

  rm -rf ${tmpdir}

else
  echo "image ${diskimg} already exists."
  echo "rm if you wish to recreate it and all of its derivatives."
fi

