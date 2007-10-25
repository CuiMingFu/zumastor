#!/bin/sh -x
#
# $Id$
#
#
# Continuously svn update, run the encapsulated build
# Create symlinks to the new debs each time a new build is successful,
# so other continuous-* scripts can tell at a glance that a new one
# has arrived.

mailto=/usr/bin/mailto
sendmail=/usr/sbin/sendmail
biabam=/usr/bin/biabam
TUNBR=tunbr
email_failure="zumastor-buildd@google.com"
email_success="zumastor-buildd@google.com"
repo="${PWD}/zumastor"
top="${PWD}"

branch=`cat $repo/Version`

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
  subject="zumastor b$branch r$revision build success"
  files="$buildlog"
  email="${email_success}"
  # store the revision just built in a symlink for use by readlink
  # in the installer stage running in a separate loop
  ln -sf $revision ${top}/buildrev
else
  subject="zumastor b$branch r$revision build failure $buildret"
  files="$buildlog"
  email="${email_failure}"
fi


# send $subject and $files to $email
if [ -x ${mailto} ] ; then
  (
    for f in $files
    do
      echo '~*'
      echo 1
      echo $f
      echo text/plain
    done
  ) | ${mailto} -s "${subject}" ${email}

elif [ -x ${biabam} ] ; then
  bfiles=`echo $files | tr ' ' ','`
  cat $files | ${biabam} $bfiles -s "${subject}" ${email}

elif [ -x ${sendmail} ] ; then
  (
    echo "Subject: " $subject
    echo
    for f in $files
    do
      cat $f
    done
  ) | ${sendmail} ${email}
fi

# loop waiting for a new update
oldrevision=${revision}
diff=`mktemp`
while true
do
  svn update
  revision=`svnversion || svn info | awk '/Revision:/ { print $2; }'`

  # the diff between the old revision and the current one
  svn diff -r$oldrevision >$diff

  # wait and loop if the revision number is the same
  if [ "x$revision" = "x$oldrevision" ]
  then
    sleep 300

  # wait and loop if the diff is zero length
  elif [ ! -s "$diff" ]
  then
    sleep 300

  else
    # restart continuous.sh, beginning a new build
    popd
    exec $0
  fi
done
