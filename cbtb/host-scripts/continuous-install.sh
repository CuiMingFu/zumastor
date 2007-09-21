#!/bin/sh -x
#
# $Id$
#

# Continuously check whether a new build has completed, and install
# the new debs into a new image to use for testing.  Create a symlink to
# the new image for the continuous-test script to notice and launch tests
# against.

buildrev=''
if [ -L ${HOME}/buildrev ] ; then
  buildrev=`readlink ${HOME}/buildrev`
fi

installrev=''
if [ -L ${HOME}/installrev ] ; then
  installrev=`readlink ${HOME}/installrev`
fi

if [ "x$buildrev" = "x$installrev" ] ; then
  # wait 5 minutes and restart the script.  Don't do anything until
  # the symlinks to the revision numbers are actually different
  # restarting allows for easily deploying changes to this script.
  sleep 300
  exec $0
fi


mailto=/usr/bin/mailto
sendmail=/usr/sbin/sendmail
biabam=/usr/bin/biabam
TUNBR=tunbr
email_failure="zumastor-buildd@google.com"
email_success="zumastor-buildd@google.com"
repo="${HOME}/zumastor"

diskimgdir=${HOME}/testenv
[ -x /etc/default/testenv ] && . /etc/default/testenv

export BUILDDIR="${HOME}/zumastor/build"
export SVNREV=$buildrev
export TEMPLATEIMG="${BUILDDIR}/dapper-i386.img"
export DISKIMG="${BUILDDIR}/dapper-i386-zumastor-r${SVNREV}.img"


pushd ${BUILDDIR}

# build and test the current working directory packages
installlog=`mktemp`
installret=-1

rm -f ${diskimg}
time ${TUNBR} \
  timeout -14 7200 \
  ${HOME}/zuma-dapper-i386.sh >${installlog} 2>&1
installret=$?
echo continuous zuma-dapper-i386 returned $installret

popd

if [ $installret -eq 0 ]; then
  subject="zumastor r$buildrev install success"
  files="$installlog"
  email="${email_success}"

  # creating this symlink only on success will cause the install step to
  # keep repeating on failure.  For the moment, while the continuous build
  # is maturing, this is desired.  Modify the logic later to not loop over
  # installation attempts.
  ln -sf $buildrev ${HOME}/installrev

else
  subject="zumastor r$revision install failure $installret"
  files="$installlog"
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
  cat $summary | ${biabam} $bfiles -s "${subject}" ${email}

elif [ -x ${sendmail} ] ; then
  (
    echo "Subject: $subject"
    echo
    for f in $files
    do
      cat $f
    done
  ) | ${sendmail} ${email}
fi

# pause 5 minutes, then restart and try again if there has been an update
sleep 300
exec $0
