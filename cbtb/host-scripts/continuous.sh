#!/bin/sh -x
#
# $Id$
#

# Continuously svn update, run the encapsulated build, create a template
# image with the build results, and the test
# suite.  Copy this and the other scripts in cbtb/host-scripts to
# the repository parent directory to avoid running unsafe code on your host
# machine.  Code direct from the repository is only run on virtual instances.

sendmail=/usr/sbin/sendmail
TUNBR=tunbr
email_failure="zumastor-buildd@google.com"
email_success="zumastor-buildd@google.com"
repo="${PWD}/zumastor"
top="${PWD}"

diskimgdir=${HOME}/testenv
[ -x /etc/default/testenv ] && . /etc/default/testenv

IMAGE=zuma-dapper-i386
IMAGEDIR=${diskimgdir}/${IMAGE}
diskimg=${IMAGEDIR}/hda.img


if [ ! -f zumastor/Changelog ] ; then
  echo "cp $0 to the parent directory of the zumastor repository and "
  echo "run from that location.  Periodically inspect the three "
  echo "host scripts for changes and redeploy them."
  exit 1
fi

pushd ${repo}

# build and test the current working directory packages
revision=`svn info | awk '/Revision:/ { print $2; }'`
buildlog=`mktemp`
installlog=`mktemp`
testlog=`mktemp`
buildret=-1
installret=-1
testret=-1

time ${TUNBR} timeout -14 39600 ${top}/dapper-build.sh >${buildlog} 2>&1
buildret=$?

if [ $buildret -eq 0 ] ; then
  rm -f ${diskimg}
  pushd cbtb/host-scripts
  time ${TUNBR} timeout -14 7200 ${top}/zuma-dapper-i386.sh >${installlog} 2>&1
  installret=$?
  popd

  if [ $installret -eq 0 ] ; then
    time timeout -14 7200 ${top}/runtests.sh >>${testlog} 2>&1
    testret=$?
  fi
fi
    
# send full logs on success for use in comparisons.
# send just the failing log on any failure with subject and to the
# possibly different failure address.
if [ $testret -eq 0 ]; then
  subject="Subject: zumastor r$revision build, install, and test success"
  files="$buildlog $installlog $testlog"
  email="${email_success}"
elif [ $installret -eq 0 ]; then
  subject="Subject: zumastor r$revision test failure $testret"
  files="$testlog"
  email="${email_failure}"
elif [ $buildret -eq 0 ]; then
  subject="Subject: zumastor r$revision install failure $installret"
  files="$installlog"
  email="${email_failure}"
else
  subject="Subject: zumastor r$revision build failure $buildret"
  files="$buildlog"
  email="${email_failure}"
fi


# send $subject and $files to $email
( echo $subject
  echo
  for f in $files
  do
    cat $f
  done
) | ${sendmail} ${email}


# loop waiting for a new update
oldrevision=${revision}
while true
do
  svn update
  revision=`svnversion || svn info | awk '/Revision:/ { print $2; }'`
  if [ "x$revision" = "x$oldrevision" ]
  then
    sleep 300
  else
    # restart continuous.sh, beginning a new build
    popd
    exec $0
  fi
done
