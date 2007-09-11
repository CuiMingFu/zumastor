#!/bin/sh -x

# Run the zuma/dapper/i386 image using -snapshot to verify that it works.
# The template should be left unmodified.
# Any parameters are copied to the destination instance and executed as root

# $Id$
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

SSH='ssh -o StrictHostKeyChecking=no'
SCP='scp -o StrictHostKeyChecking=no'
CMDTIMEOUT='timeout -14 120'

retval=0

execfiles="$*"

if [ "x$MACFILE" = "x" -o "x$MACADDR" = "x" -o "x$IFACE" = "x" \
     -o "x$IPADDR" = "x" ] ; then
  echo "Run this script under at least one tunbr"
  exit 1
fi

# defaults, overridden by /etc/default/testenv if it exists
# diskimgdir should be local for reasonable performance
diskimgdir=${HOME}/testenv
tftpdir=/tftpboot
qemu_i386=qemu  # could be kvm, kqemu version, etc.  Must be 0.9.0 to net boot.

VIRTHOST=192.168.23.1
[ -x /etc/default/testenv ] && . /etc/default/testenv

IMAGE=zuma-dapper-i386
IMAGEDIR=${diskimgdir}/${IMAGE}
diskimg=${IMAGEDIR}/hda.img

tmpdir=`mktemp -d /tmp/${IMAGE}.XXXXXX`
SERIAL=${tmpdir}/serial
MONITOR=${tmpdir}/monitor
VNC=${tmpdir}/vnc


if [ ! -f ${diskimg} ] ; then

  echo "No template image ${diskimg} exists yet."
  echo "Run tunbr zuma-dapper-i386.sh first."
  exit 1
fi

echo IPADDR=${IPADDR}
echo control/tmp dir=${tmpdir}

${qemu_i386} -snapshot \
  -serial unix:${SERIAL},server,nowait \
  -monitor unix:${MONITOR},server,nowait \
  -vnc unix:${VNC} \
  -net nic,macaddr=${MACADDR},model=ne2k_pci \
  -net tap,ifname=${IFACE},script=no \
  -boot c -hda ${diskimg} -no-reboot & qemu_pid=$!

if [ "x$MACADDR2" != "x" ] ; then
  SERIAL2=${tmpdir}/serial2
  MONITOR2=${tmpdir}/monitor2
  VNC2=${tmpdir}/vnc2
  ${qemu_i386} -snapshot \
    -serial unix:${SERIAL2},server,nowait \
    -monitor unix:${MONITOR2},server,nowait \
    -vnc unix:${VNC2} \
    -net nic,macaddr=${MACADDR2},model=ne2k_pci \
    -net tap,ifname=${IFACE2},script=no \
    -boot c -hda ${diskimg} -no-reboot & qemu2_pid=$!
fi


# kill the emulator if any abort-like signal is received
trap "kill -9 ${qemu_pid} ${qemu2_pid} ; exit 1" 1 2 3 6 14 15

while ! ${SSH} root@${IPADDR} hostname 2>/dev/null
do
  echo -n .
  sleep 10
done


if [ "x$MACADDR2" != "x" ] ; then
  while ! ${SSH} root@${IPADDR2} hostname 2>/dev/null
  do
    echo -n .
    sleep 10
  done
fi

echo IPADDR=${IPADDR} IPADDR2=${IPADDR2}
echo control/tmp dir=${tmpdir}

params="IPADDR=${IPADDR}"
if [ "x$IPADDR2" != "x" ] ; then
  params="${params} IPADDR2=${IPADDR2}"
fi

# execute any parameters here
if [ "x${execfiles}" != "x" ]
then
  ${CMDTIMEOUT} ${SCP} ${execfiles} root@${IPADDR}: </dev/null || true
  for f in ${execfiles}
  do
    timelimit=`awk -F = '/^TIMEOUT=[0-9]+$/ { print $2; }' ./${f} | tail -1`
    if [ "x$timelimit" = "x" ] ; then
      timeout=""
    else
      timeout="timeout -14 $timelimit"
    fi
    if ${timeout} ${SSH} root@${IPADDR} ${params} ./${f}
    then
      echo ${f} ok
    else
      retval=$?
      echo ${f} not ok
    fi
  done
fi


${CMDTIMEOUT} ${SSH} root@${IPADDR} poweroff

if [ "x$IPADDR2" != "x" ] ; then
  ${CMDTIMEOUT} ${SSH} root@${IPADDR2} poweroff
fi


sed -i /^${IPADDR}\ .*\$/d ~/.ssh/known_hosts || true
if [ "x$IPADDR2" != "x" ] ; then
  sed -i /^${IPADDR2}\ .*\$/d ~/.ssh/known_hosts || true
fi

time wait ${qemu_pid} || retval=$?
kill -0 ${qemu_pid} && kill -9 ${qemu_pid}

if [ "x$qemu2_pid" != "x" ] ; then
  time wait ${qemu2_pid} || retval=$?
  kill -0 ${qemu2_pid} && kill -9 ${qemu2_pid}
fi

rm -rf %{tmpdir}

exit ${retval}
