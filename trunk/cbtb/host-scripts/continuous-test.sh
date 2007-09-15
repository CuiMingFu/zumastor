#!/bin/sh -x
#
# $Id$
#
#
# continuously run an svn update on a zumastor repository with the cbtb/tests
# Whenever the last successful test revision differs from the last successful
# install revision, fire off a new round of tests.

installrev=''
if [ -L installrev ] ; then
  installrev=`readlink installrev`
fi
  
testrev=''
if [ -L testrev ] ; then
  testrev=`readlink testrev`
fi
    
if [ "x$installrev" = "x$testrev" ] ; then
  # wait 5 minutes and restart the script.  Don't do anything until
  # the symlinks to the revision numbers are actually different
  # restarting allows for easily deploying changes to this script.
  sleep 300
  exec $0
fi
          
          
mailto=/usr/bin/mailto
sendmail=/usr/sbin/sendmail
TUNBR=tunbr
email_failure="zumastor-buildd@google.com"
email_success="zumastor-buildd@google.com"
repo="${HOME}/zumastor-tests"
top="${HOME}"

diskimgdir=${HOME}/testenv
[ -x /etc/default/testenv ] && . /etc/default/testenv

IMAGE=zuma-dapper-i386
IMAGEDIR=${diskimgdir}/${IMAGE}
diskimg=${IMAGEDIR}/hda.img



if [ ! -d ${repo} ] ; then
  echo "svn checkout http://zumastor.googlecode.com/svn/trunk/cbtb/tests zumastor-tests"
  echo "cp $0 to the parent directory of the zumastor-tests repository and "
  echo "run from that location.  Periodically inspect the cbtb/host-scripts"
  echo "for changes and redeploy them."
  exit 1
fi

pushd ${repo}

# build and test the last successfully install revision
svn update -r $installrev

testlog=`mktemp`
testret=-1

time timeout -14 7200 ${top}/runtests.sh >>${testlog} 2>&1
testret=$?
echo continuous runtests returned $testret
    
# send full logs on success for use in comparisons.
# send just the failing log on any failure with subject and to the
# possibly different failure address.
if [ $testret -eq 0 ]; then
  subject="Szumastor r$installrev test success"
  files="$testlog"
  email="${email_success}"

  # creating this symlink only on success will cause the test step to
  # keep repeating on failure.  For the moment, while the continuous build
  # is maturing, this is desired.  Modify the logic later to not loop over
  # test attempts.
  ln -sf $installrev ${TOP}/testrev

else
  subject="zumastor r$installrev test failure $testret"
  files="$testlog"
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

# loop and reload the script
sleep 300
exec $0
