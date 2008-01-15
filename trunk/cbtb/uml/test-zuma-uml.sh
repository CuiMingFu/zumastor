#!/bin/sh -x

# Run the zumastor distribution and arch ext3 image using UML COW verify that it works.
# The template should be left unmodified.
# Any parameters are copied to the destination instance and executed as root
# DIST and ARCH default to dapper/i386. but may be specified.  Currently
# only DIST=etch, ARCH=i386 is also supported.

# $Id$
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

if [ "x$ARCH" = "x" ] ; then
  ARCH=i386
fi

if [ "x$DIST" = "x" ] ; then
  DIST=dapper
fi


pushd ../..
  repo=${PWD}
  build=${PWD}/build
  SVNREV=`awk '/^[0-9]+$/ { print $1; }' SVNREV || svnversion | tr [A-Z] [a-z] || svn info zumastor | grep ^Revision:  | cut -d\  -f2`
popd

templateimg=$build/${DIST}-${ARCH}-zumastor-r${SVNREV}.ext3

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

VIRTHOST=192.168.23.1
[ -x /etc/default/testenv ] && . /etc/default/testenv

tmpdir=`mktemp -d /tmp/zuma-uml.XXXXXX`

logdir="$build/r${SVNREV}"
[ -d $logdir ] || mkdir $logdir

# scrape HD[BCD]SIZE from the test script, create,
# and store qemu parameters in $qemu_hd and $qemu2_hd in the $tmpdir.
largest_hdbsize=0
largest_hdcsize=0
largest_hddsize=0
for f in ${execfiles}
do
  if [ "x$console1" = "x" ] ; then console1="$logdir/`basename ${f}`.console1" ; fi
  if [ "x$console2" = "x" ] ; then console2="$logdir/`basename ${f}`.console2" ; fi
  hdbsize=`awk -F = '/^HDBSIZE=[0-9]+$/ { print $2; }' ./${f} | tail -1`
  if [ "x$hdbsize" != "x" ] ; then
    if [ "$hdbsize" -ge "$largest_hdbsize" ] ; then
      largest_hdbsize=$hdbsize
    fi
  fi
  hdcsize=`awk -F = '/^HDCSIZE=[0-9]+$/ { print $2; }' ./${f} | tail -1`
  if [ "x$hdcsize" != "x" ] ; then
    if [ "$hdcsize" -ge "$largest_hdcsize" ] ; then
      largest_hdcsize=$hdcsize
    fi
  fi
  hddsize=`awk -F = '/^HDDSIZE=[0-9]+$/ { print $2; }' ./${f} | tail -1`
  if [ "x$hddsize" != "x" ] ; then
    if [ "$hddsize" -ge "$largest_hddsize" ] ; then
      largest_hddsize=$hddsize
    fi
  fi
done

if [ "x$console1" = "x" ] ; then console1="$logdir/console1" ; fi
if [ "x$console2" = "x" ] ; then console2="$logdir/console2" ; fi


hd=""
hd2=""
if [ $largest_hdbsize -gt 0 ] ; then
  dd of=${tmpdir}/hdb.img bs=1M seek=${largest_hdbsize} count=0 if=/dev/zero
  hd="ubd1=${tmpdir}/hdb.img"
  if [ "x$MACADDR2" != "x" ] ; then
    dd of=${tmpdir}/hdb2.img bs=1M seek=${largest_hdbsize} count=0 if=/dev/zero
    hd2="ubd1=${tmpdir}/hdb2.img"
  fi
fi
if [ $largest_hdcsize -gt 0 ] ; then
  dd of=${tmpdir}/hdc.img bs=1M seek=${largest_hdcsize} count=0 if=/dev/zero
  hd="${hd} ubd2=${tmpdir}/hdc.img"
  if [ "x$MACADDR2" != "x" ] ; then
    dd of=${tmpdir}/hdc2.img bs=1M seek=${largest_hdcsize} count=0 if=/dev/zero
    hd2="${hd2} ubd2=${tmpdir}/hdc2.img"
  fi
fi
if [ $largest_hddsize -gt 0 ] ; then
  dd of=${tmpdir}/hdd.img bs=1M seek=${largest_hddsize} count=0 if=/dev/zero
  hd="${hd} ubd3=${tmpdir}/hdd.img"
  if [ "x$MACADDR2" != "x" ] ; then
    dd of=${tmpdir}/hdd2.img bs=1M seek=${largest_hddsize} count=0 if=/dev/zero
    hd2="${hd2} ubd3=${tmpdir}/hdd2.img"
  fi
fi

$build/linux-${ARCH}-r${SVNREV} \
  ubd0=${tmpdir}/hda.img,$templateimg $hd fake_ide \
  mem=512M \
  eth0=tuntap,$IFACE,$MACADDR,$VIRTHOST \
  & uml_pid=$!
#  con0=fd:0,fd:1 con1=fd:0,fd:1 con=null </dev/null >$console1 \

if [ "x$MACADDR2" != "x" ] ; then
  $build/linux-${ARCH}-r${SVNREV} \
    ubd0=${tmpdir}/hda2.img,$templateimg $hd2 fake_ide \
    mem=512M \
    eth0=tuntap,$IFACE2,$MACADDR2,$VIRTHOST \
    & uml2_pid=$!
#    con0=fd:0,fd:1 con1=fd:0,fd:1 con=null </dev/null >$console2 
fi


# kill the emulator if any abort-like signal is received
trap "kill ${uml_pid} ${uml2_pid} ; exit 1" 1 2 3 6 14 15

count=0
while kill -0 ${uml_pid} && [ $count -lt 30 ] && \
  ! ${SSH} root@${IPADDR} hostname 2>/dev/null
do
  count=$(( count + 1 ))
  sleep 1
done
if [ $count -ge 30 ]
then
  kill $uml_pid
  retval=64
  unset uml_pid
fi
  
if [ "x$MACADDR2" != "x" ] ; then
  count=0
  while kill -0 ${uml2_pid} && [ $count -lt 30 ] && \
    ! ${SSH} root@${IPADDR2} hostname 2>/dev/null
  do
    count=$(( count + 1 ))
    sleep 1
  done

  if [ $count -ge 30 ] 
  then
    kill $uml2_pid
    retval=65
    unset uml2_pid
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
    # scrape TIMEOUT from the test script and store in the $timeout variable
    timelimit=`awk -F = '/^TIMEOUT=[0-9]+$/ { print $2; }' ./${f} | tail -1`
    if [ "x$timelimit" = "x" ] ; then
      timeout=""
    else
#      timeout="timeout -14 $timelimit"
      timeout=""
    fi

    if ${timeout} ${SSH} root@${IPADDR} ${params} ./`basename ${f}`
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
if [ "x$uml_pid" != "x" ] ; then
  ( sleep 600 ; kill $uml_pid ) & killer=$!
fi
if [ "x$uml2_pid" != "x" ] ; then
  ( sleep 600 ; kill $uml2_pid ) & killer2=$!
fi

if [ "x$uml_pid" != "x" ] ; then
  ${CMDTIMEOUT} ${SSH} root@${IPADDR} poweroff
fi

if [ "x$uml2_pid" != "x" ] ; then
  ${CMDTIMEOUT} ${SSH} root@${IPADDR2} poweroff
fi


sed -i /^${IPADDR}\ .*\$/d ~/.ssh/known_hosts || true
if [ "x$IPADDR2" != "x" ] ; then
  sed -i /^${IPADDR2}\ .*\$/d ~/.ssh/known_hosts || true
fi

if [ "x$uml_pid" != "x" ] ; then
  time wait ${uml_pid} || retval=$?
  kill -0 ${uml_pid} && kill ${uml_pid}
fi

if [ "x$uml2_pid" != "x" ] ; then
  time wait ${uml2_pid} || retval=$?
  kill -0 ${uml2_pid} && kill ${uml2_pid}
fi


# clean up the 10 minute shutdown killers
if [ "x$killer2" != "x" ] ; then
  kill -0 $killer && kill $killer
fi
if [ "x$killer2" != "x" ] ; then
  kill -0 $killer2 && kill $killer2
fi

rm -rf ${tmpdir}

exit ${retval}
