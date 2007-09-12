#!/bin/sh -x
#
# $Id$
#
# Launch a dapper-i386 snapshot
# create a build user in the snapshot
# copy the zumastor source to ~build
# run buildcurrent.sh as user build in the testenv instance
# pull the debs out of the instance
# shut down the instance
#
# Copy this to a directory outside the working directory, and launch
# with the top level of the working directory
#
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e
rc=0

KERNEL_VERSION=`awk '/^2\.6\.[0-9]+(\.[0-9]+)?$/ { print $1; }' KernelVersion`
if [ "x$KERNEL_VERSION" = "x" ] ; then
  echo "Suspect KernelVersion file"
  exit 64
fi
VERSION=`awk '/[0-9]+\.[0-9]+(\.[0-9]+)?$/ { print $1; }' Version`
if [ "x$VERSION" = "x" ] ; then
  echo "Suspect Version file"
  exit 65
fi
SVNREV=`svnversion || svn info | awk '/Revision:/ { print $2; }'`
ARCH=i386


if [ "x$MACFILE" = "x" -o "x$MACADDR" = "x" -o "x$IFACE" = "x" ] ; then
  echo "Run this script under tunbr"
  exit 66
fi

SSH='ssh -o StrictHostKeyChecking=no'
SCP='time timeout -14 3600 scp -o StrictHostKeyChecking=no'
CMDTIMEOUT='time timeout -14 120'
BUILDTIMEOUT='time timeout -14 36000'
SETUPTIMEOUT='time timeout -14 3600'
WGETTIMEOUT='time timeout -14 3600'


# defaults, overridden by /etc/default/testenv if it exists
# diskimgdir should be local for reasonable performance
size=10G
diskimgdir=${HOME}/testenv
tftpdir=/tftpboot
qemu_i386=qemu  # could be kvm, kqemu version, etc.  Must be 0.9.0 to net boot.
qemu_threads=1

[ -x /etc/default/testenv ] && . /etc/default/testenv

threads=$(($qemu_threads + 1))
mem=$(($threads * 128 + 768))

IMAGE=dapper-i386
IMAGEDIR=${diskimgdir}/${IMAGE}
diskimg=${IMAGEDIR}/hda.img

tmpdir=`mktemp -d /tmp/${IMAGE}.XXXXXX`
SERIAL=${tmpdir}/serial
MONITOR=${tmpdir}/monitor
VNC=${tmpdir}/vnc


if [ ! -f ${diskimg} ] ; then
  echo "No $diskimg available.  Run dapper-i386.sh first."
  exit 67
fi

echo IPADDR=${IPADDR}
echo control/tmp dir=${tmpdir}

${qemu_i386} -snapshot -m ${mem} -smp ${qemu_threads} \
  -serial unix:${SERIAL},server,nowait \
  -monitor unix:${MONITOR},server,nowait \
  -vnc unix:${VNC} \
  -net nic,macaddr=${MACADDR} -net tap,ifname=${IFACE},script=no \
  -boot c -hda ${diskimg} -no-reboot & qemu=$!

# kill the emulator if any abort-like signal is received
trap "kill -9 ${qemu} ; exit 68" 1 2 3 6 14 15

while ! ${SSH} root@${IPADDR} hostname >/dev/null 2>&1
do
  echo -n .
  sleep 10
done

if [ ! -d build ] ; then
  mkdir build
fi

pushd build
if [ ! -f linux-${KERNEL_VERSION}.tar.bz2 ] ; then
  ${WGETTIMEOUT} wget -c http://www.kernel.org/pub/linux/kernel/v2.6/linux-${KERNEL_VERSION}.tar.bz2
fi
popd

${SETUPTIMEOUT} ${SSH} root@${IPADDR} <<EOF
lvcreate --name home --size 5G sysvg
mke2fs /dev/sysvg/home
mount /dev/sysvg/home /home
useradd build
mkdir -p ~build/.ssh ~build/zumastor/build
cp ~/.ssh/authorized_keys ~build/.ssh/
chown -R build ~build
EOF

tar cf - --exclude build * | \
  ${SETUPTIMEOUT} ${SSH} build@${IPADDR} tar xf - -C zumastor
${SCP} build/linux-${KERNEL_VERSION}.tar.bz2 build@${IPADDR}:zumastor/build/

${SETUPTIMEOUT} ${SSH} root@${IPADDR} <<EOF 
cd ~build/zumastor
./builddepends.sh
echo CONCURRENCY_LEVEL := ${threads} >> /etc/kernel-pkg.conf
EOF

# Specific kernel configurations take priority over general configurations
# kernel/config/${KERNEL_VERSION}-${ARCH} is not in the archive and may
# be a symlink to specify a specific kernel config in the local repository
# (eg. qemu-only build)
for kconf in \
  kernel/config/full \
  kernel/config/qemu \
  kernel/config/default \
  kernel/config/${KERNEL_VERSION}-${ARCH}-full \
  kernel/config/${KERNEL_VERSION}-${ARCH}-qemu \
  kernel/config/${KERNEL_VERSION}-${ARCH}
do
  if [ -e ${kconf} ] ; then
    KERNEL_CONFIG=${kconf}
  fi
done

time ${CMDTIMEOUT} \
  ${SSH} build@${IPADDR} "echo $SVNREV >zumastor/SVNREV" || rc=$?

# give the build 3 hours, then kill it.
time ${BUILDTIMEOUT} \
  ${SSH} build@${IPADDR} "cd zumastor && ./buildcurrent.sh $KERNEL_CONFIG" || \
  rc=$?

BUILDSRC="build@${IPADDR}:zumastor/build"
DEBVERS="${VERSION}-r${SVNREV}"
KVERS="${KERNEL_VERSION}-zumastor-r${SVNREV}_1.0"

for f in \
    ${BUILDSRC}/ddsnap_${DEBVERS}_${ARCH}.changes \
    ${BUILDSRC}/ddsnap_${DEBVERS}_${ARCH}.deb \
    ${BUILDSRC}/ddsnap_${DEBVERS}.dsc \
    ${BUILDSRC}/ddsnap_${DEBVERS}.tar.gz \
    ${BUILDSRC}/zumastor_${DEBVERS}_${ARCH}.changes \
    ${BUILDSRC}/zumastor_${DEBVERS}_${ARCH}.deb \
    ${BUILDSRC}/zumastor_${DEBVERS}.dsc \
    ${BUILDSRC}/zumastor_${DEBVERS}.tar.gz \
    ${BUILDSRC}/kernel-headers-${KVERS}_${ARCH}.deb \
    ${BUILDSRC}/kernel-image-${KVERS}_${ARCH}.deb
do
  time ${SCP} $f build/ || rc=$?
done

${CMDTIMEOUT} ${SSH} root@${IPADDR} poweroff

#socat STDIN UNIX-CONNECT:${MONITOR} <<EOF
#quit
#EOF

time wait $qemu || rc=$?
kill -0 $qemu && kill -9 $qemu

rm -rf ${tmpdir}

exit ${rc}
