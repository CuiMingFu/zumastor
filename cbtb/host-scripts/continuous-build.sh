#!/bin/sh -x
#
# $Id$
#

# Continuously svn update, run the encapsulated build
# Create symlinks to the new debs each time a new build is successful,
# so other continuous-* scripts can tell at a glance that a new one
# has arrived.

mailto=/usr/bin/mailto
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
buildret=-1


time ${TUNBR} timeout -14 39600 ${top}/dapper-build.sh >${buildlog} 2>&1
buildret=$?
echo continuous dapper-build returned $buildret



if [ $buildret -eq 0 ]; then
  subject="Subject: zumastor r$revision build success"
  files="$buildlog"
  email="${email_success}"
  # store the revision just built in a symlink for use by readlink
  # in the installer stage running in a separate loop
  ln -sf $revision buildrev
else
  subject="Subject: zumastor r$revision build failure $buildret"
  files="$buildlog"
  email="${email_failure}"
fi


# send $subject and $files to $email
(
  for f in $files
  do
    echo '~*'
    echo 1
    echo $f
    echo text/plain
  done
) | ${mailto} -s "${subject}" ${email}


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
