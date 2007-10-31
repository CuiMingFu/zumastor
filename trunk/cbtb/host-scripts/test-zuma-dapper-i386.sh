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
# CMDTIMEOUT='timeout -14 120'
CMDTIMEOUT=''

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
BUILDSRC=${top}/zumastor/build
if [ "x$DISKIMG" = "x" ] ; then
  diskimg=${IMAGEDIR}/hda.img
else
  diskimg="$DISKIMG"
fi

tmpdir=`mktemp -d /tmp/${IMAGE}.XXXXXX`
MONITOR=${tmpdir}/monitor
VNC=${tmpdir}/vnc
if [ "x$LOGDIR" != "x" ] ; then
  SERIAL=${LOGDIR}/${LOGPREFIX}serial
  SERIAL2=${LOGDIR}/${LOGPREFIX}serial2
else
  SERIAL=${tmpdir}/serial
  SERIAL2=${tmpdir}/serial2
fi


if [ ! -f ${diskimg} ] ; then

  echo "No template image ${diskimg} exists yet."
  echo "Run tunbr zuma-dapper-i386.sh first."
  exit 1
fi

echo IPADDR=${IPADDR}
echo control/tmp dir=${tmpdir}

${qemu_i386} -snapshot \
  -serial file:${SERIAL} \
  -monitor unix:${MONITOR},server,nowait \
  -vnc unix:${VNC} \
  -net nic,macaddr=${MACADDR},model=ne2k_pci \
  -net tap,ifname=${IFACE},script=no \
  -boot c -hda ${diskimg} -no-reboot & qemu_pid=$!

if [ "x$MACADDR2" != "x" ] ; then
  MONITOR2=${tmpdir}/monitor2
  VNC2=${tmpdir}/vnc2
  ${qemu_i386} -snapshot \
    -serial file:${SERIAL2} \
    -monitor unix:${MONITOR2},server,nowait \
    -vnc unix:${VNC2} \
    -net nic,macaddr=${MACADDR2},model=ne2k_pci \
    -net tap,ifname=${IFACE2},script=no \
    -boot c -hda ${diskimg} -no-reboot & qemu2_pid=$!
fi


# kill the emulator if any abort-like signal is received
trap "kill -9 ${qemu_pid} ${qemu2_pid} ; exit 1" 1 2 3 6 14 15

count=0
while [ $count -lt 30 ] && ! ${SSH} root@${IPADDR} hostname 2>/dev/null
do
  count=$(( count + 1 ))
  echo -n .
  sleep 10
done
if [ $count -ge 30 ]
then
  if [ "x$LOGDIR" != "x" ] ; then
    socat - unix:${MONITOR} <<EOF
screendump $LOGDIR/${LOGPREFIX}screen.ppm
EOF
    convert $LOGDIR/${LOGPREFIX}screen.ppm $LOGDIR/${LOGPREFIX}screen.png
    rm $LOGDIR/${LOGPREFIX}screen.ppm
  fi
  kill -9 $qemu_pid
  retval=64
  unset qemu_pid
fi
  
if [ "x$MACADDR2" != "x" ] ; then
  count=0
  while [ $count -lt 30 ] && ! ${SSH} root@${IPADDR2} hostname 2>/dev/null
  do
    count=$(( count + 1 ))
    echo -n .
    sleep 10
  done

  if [ $count -ge 30 ] 
  then
    if [ "x$LOGDIR" != "x" ] ; then
      socat - unix:${MONITOR2} <<EOF
screendump $LOGDIR/${LOGPREFIX}screen2.ppm
EOF
      convert $LOGDIR/${LOGPREFIX}screen2.ppm $LOGDIR/${LOGPREFIX}screen2.png
      rm $LOGDIR/${LOGPREFIX}screen2.ppm
    fi
    kill -9 $qemu2_pid
    retval=65
    unset qemu2_pid
  fi
fi

params="IPADDR=${IPADDR}"
if [ "x$IPADDR2" != "x" ] ; then
  params="${params} IPADDR2=${IPADDR2}"
fi


# execute any parameters here, but only if all instances booted
if [ "x${execfiles}" != "x" ] && [ $retval -eq 0 ]
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


# Kill emulators if more than 10 minutes pass during shutdown
# They haven't been dying properly
if [ "x$qemu_pid" != "x" ] ; then
  ( sleep 600 ; kill -9 $qemu_pid ) & killer=$!
fi
if [ "x$qemu2_pid" != "x" ] ; then
  ( sleep 600 ; kill -9 $qemu2_pid ) & killer2=$!
fi

if [ "x$qemu_pid" != "x" ] ; then
  ${CMDTIMEOUT} ${SSH} root@${IPADDR} poweroff
fi

if [ "x$qemu2_pid" != "x" ] ; then
  ${CMDTIMEOUT} ${SSH} root@${IPADDR2} poweroff
fi


sed -i /^${IPADDR}\ .*\$/d ~/.ssh/known_hosts || true
if [ "x$IPADDR2" != "x" ] ; then
  sed -i /^${IPADDR2}\ .*\$/d ~/.ssh/known_hosts || true
fi

if [ "x$qemu_pid" != "x" ] ; then
  time wait ${qemu_pid} || retval=$?
  kill -0 ${qemu_pid} && kill -9 ${qemu_pid}
fi

if [ "x$qemu2_pid" != "x" ] ; then
  time wait ${qemu2_pid} || retval=$?
  kill -0 ${qemu2_pid} && kill -9 ${qemu2_pid}
fi


# clean up the 10 minute shutdown killers
if [ "x$killer2" != "x" ] ; then
  kill -0 $killer && kill -9 $killer
fi
if [ "x$killer2" != "x" ] ; then
  kill -0 $killer2 && kill -9 $killer2
fi

rm -rf ${tmpdir}

exit ${retval}
