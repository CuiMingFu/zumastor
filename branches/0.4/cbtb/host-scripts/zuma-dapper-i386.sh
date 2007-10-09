#!/bin/sh -x
#
# Build an image with current or provided zumastor debs installed, booted,
# and ready to immediately run single-node tests.
# Inherits from the generic dapper template.
#
# $Id$
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

KERNEL_VERSION=`awk '/^2\.6\.[0-9]+(\.[0-9]+)?$/ { print $1; }' ../KernelVersion`
if [ "x$KERNEL_VERSION" = "x" ] ; then
  echo "Suspect KernelVersion file"
  exit 1
fi
VERSION=`awk '/[0-9]+\.[0-9]+(\.[0-9]+)?$/ { print $1; }' ../Version`
if [ "x$VERSION" = "x" ] ; then
  echo "Suspect Version file"
  exit 1
fi
if [ "x$SVNREV" = "x" ] ; then
  pushd ..
  SVNREV=`svnversion || svn info | awk '/Revision:/ { print $2; }'`
  popd
fi
ARCH=i386
DEBVERS="${VERSION}-r${SVNREV}"
KVERS="${KERNEL_VERSION}-zumastor-r${SVNREV}_1.0"

SSH='ssh -o StrictHostKeyChecking=no'
SCP='timeout -14 1800 scp -o StrictHostKeyChecking=no'
# CMDTIMEOUT='time timeout -14 120'
# KINSTTIMEOUT='time timeout -14 1200'
# SHUTDOWNTIMEOUT='time timeout -14 300'
CMDTIMEOUT=''
KINSTTIMEOUT=''
SHUTDOWNTIMEOUT=''

retval=0

if [ "x$MACFILE" = "x" -o "x$MACADDR" = "x" -o "x$IFACE" = "x" \
     -o "x$IPADDR" = "x" ] ; then
  echo "Run this script under tunbr"
  exit 1
fi

# defaults, overridden by /etc/default/testenv if it exists
# diskimgdir should be local for reasonable performance
diskimgdir=${HOME}/testenv
tftpdir=/tftpboot
qemu_img=qemu-img  # could be kvm, kqemu version, etc.
qemu_i386=qemu  # could be kvm, kqemu version, etc.
rqemu_i386=qemu  # could be kvm, kqemu version, etc.  Must be 0.9.0 to net boot
VIRTHOST=192.168.23.1
[ -x /etc/default/testenv ] && . /etc/default/testenv

IMAGE=zuma-dapper-i386
IMAGEDIR=${diskimgdir}/${IMAGE}

if [ "x$DISKIMG" = "x" ] ; then
  DISKIMG=./hda.img
fi
  
SERIAL=${IMAGEDIR}/serial
MONITOR=${IMAGEDIR}/monitor
VNC=${IMAGEDIR}/vnc

if [ ! -e ${IMAGEDIR} ]; then
  mkdir -p ${IMAGEDIR}
fi

if [ "x$TEMPLATEIMG" = "x" ] ; then

  TEMPLATEIMG=./dapper-i386.img

  # dereference, so multiple templates may coexist simultaneously
  if [ -L "${TEMPLATEIMG}" ] ; then
    TEMPLATEIMG=`readlink ${TEMPLATEIMG}`
  fi

  if [ ! -f "${TEMPLATEIMG}" ] ; then
    echo "No template image ${TEMPLATEIMG} exists yet."
    echo "Run tunbr dapper-i386.sh first."
    exit 1
  fi
fi

if [ -f "${DISKIMG}" ] ; then
  echo Zuma/dapper image already exists, remove if you wish to build a new one
  echo rm "${DISKIMG}"
  exit 2
fi

templatedir=`dirname "${TEMPLATEIMG}"`
diskimgdir=`dirname "${TEMPLATEIMG}"`
if [ "x$templatedir" = "x$diskimgdir" ] ; then
  pushd $templatedir
  ${qemu_img} create  -b `basename "${TEMPLATEIMG}"` -f qcow2 `basename "${DISKIMG}"`
  popd
else
  ${qemu_img} create  -b "${TEMPLATEIMG}" -f qcow2 "${DISKIMG}"
fi

${qemu_i386} -m 512 \
  -serial unix:${SERIAL},server,nowait \
  -monitor unix:${MONITOR},server,nowait \
  -vnc unix:${VNC} \
  -net nic,macaddr=${MACADDR},model=ne2k_pci \
  -net tap,ifname=${IFACE},script=no \
  -boot c -hda "${DISKIMG}" -no-reboot & qemu=$!
  

# wait for ssh to work
while ! ${SSH} root@${IPADDR} hostname 2>/dev/null
do
  echo -n .
  sleep 10
done

date

# strip the root password off the specific test image.
# Makes it portable to anyone.
${SSH} root@${IPADDR} 'sed -i s/^root:x:/root::/ /etc/passwd'

# create a tmpfs /tmp on the instance to place the debs into
${SSH} root@${IPADDR} 'mount -t tmpfs tmpfs /tmp'

# copy the debs that were built in the build directory
# onto the new zuma template instance
for f in \
    ddsnap_${DEBVERS}_${ARCH}.deb \
    zumastor_${DEBVERS}_${ARCH}.deb \
    kernel-headers-${KVERS}_${ARCH}.deb \
    kernel-image-${KVERS}_${ARCH}.deb
do
  ${SCP} $f root@${IPADDR}:/tmp || retval=$?
done

# install the copied debs in the correct order
${CMDTIMEOUT} ${SSH} root@${IPADDR} aptitude install -y tree || retval=$?
${KINSTTIMEOUT} ${SSH} root@${IPADDR} dpkg -i /tmp/kernel-image-${KVERS}_${ARCH}.deb || retval=$?
${CMDTIMEOUT} ${SSH} root@${IPADDR} dpkg -i /tmp/ddsnap_${DEBVERS}_${ARCH}.deb || retval=$?
${CMDTIMEOUT} ${SSH} root@${IPADDR} dpkg -i /tmp/zumastor_${DEBVERS}_${ARCH}.deb || retval=$?
${CMDTIMEOUT} ${SSH} root@${IPADDR} 'rm /tmp/*.deb' || retval=$?
${CMDTIMEOUT} ${SSH} root@${IPADDR} apt-get clean || retval=$?

# temporary hack before going to LABEL= or UUID=
${CMDTIMEOUT} ${SSH} root@${IPADDR} 'sed -i s/hda/sda/ /boot/grub/menu.lst' || retval=$?
${CMDTIMEOUT} ${SSH} root@${IPADDR} 'sed -i s/hda/sda/ /etc/fstab' || retval=$?

# halt the new image, and wait for qemu to exit
${CMDTIMEOUT} ${SSH} root@${IPADDR} poweroff

time wait $qemu || retval=$?
kill -0 $qemu && kill -9 $qemu


if false
then

  ${qemu_i386} \
    -serial unix:${SERIAL},server,nowait \
    -monitor unix:${MONITOR},server,nowait \
    -vnc unix:${VNC} \
    -net nic,macaddr=${MACADDR} -net tap,ifname=${IFACE},script=no \
    -boot c -hda ${DISKIMG} -no-reboot & qemu=$!

  while ! ${SSH} root@${IPADDR} hostname 2>/dev/null
  do
    echo -n .
    sleep 10
  done

  socat STDIN UNIX-CONNECT:${MONITOR} <<EOF
stop
savevm booted
quit
EOF

  time wait $qemu

fi

echo "Instance shut down, removing ssh hostkey"
sed -i /^${IPADDR}\ .*\$/d ~/.ssh/known_hosts || true

exit $retval


