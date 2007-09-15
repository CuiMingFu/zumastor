#!/bin/sh -x
#
# $Id$
#

# Continuously check whether a new build has completed, and install
# the new debs into a new image to use for testing.  Create a symlink to
# the new image for the continuous-test script to notice and launch tests
# against.

buildrev=''
if [ -e buildrev ] ; then
  buildrev=`readlink buildrev`
fi

testrev=''
if [ -e testrev ] ; then
  testrev=`readlink testrev`
fi

if [ "x$buildrev" = "x$testrev" ] ; then
  # wait 5 minutes and restart the script.  Don't do anything until
  # the symlinks to the revision numbers are actually different
  # restarting allows for easily deploying changes to this script.
  sleep 300
  exec $0
fi


mailto=/usr/sbin/mailto
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


pushd ${repo}

# build and test the current working directory packages
installlog=`mktemp`
installret=-1

rm -f ${diskimg}
pushd cbtb/host-scripts
time ${TUNBR} \
  timeout -14 7200 \
  SVNREV=$buildrev ${top}/zuma-dapper-i386.sh >${installlog} 2>&1
installret=$?
echo continuous zuma-dapper-i386 returned $installret
popd

if [ $installret -eq 0 ]; then
  subject="Subject: zumastor r$revision install success"
  files="$installlog"
  email="${email_success}"

  # creating this symlink only on success will cause the install step to
  # keep repeating on failure.  For the moment, while the continuous build
  # is maturing, this is desired.  Modify the logic later to not loop over
  # installation attempts.
  ln -sf $buildrev ${TOP}/installrev

else
  subject="Subject: zumastor r$revision install failure $installret"
  files="$installlog"
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

# pause 5 minutes, then restart and try again if there has been an update
sleep 300
exec $0
