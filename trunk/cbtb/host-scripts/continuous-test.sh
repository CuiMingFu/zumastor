#!/bin/sh -x
#
# $Id$
#
#
# continuously run an svn update on a zumastor repository with the cbtb/tests
# Whenever the last successful test revision differs from the last successful
# install revision, fire off a new round of tests.

top="${PWD}"
branch=`cat $top/zumastor/Version`

installrev=''
if [ -L ${top}/installrev ] ; then
  installrev=`readlink ${top}/installrev`
fi
  
testrev=''
if [ -L ${top}/testrev ] ; then
  testrev=`readlink ${top}/testrev`
fi
    
if [ "x$installrev" = "x$testrev" ] ; then
  # wait 5 minutes and restart the script.  Don't do anything until
  # the symlinks to the revision numbers are actually different
  # restarting allows for easily deploying changes to this script.
  sleep 300
  exec $0
fi

if [ "x$FAILED_TEST_REV" = "x$installrev" ] ; then
  # if the last install test failed, FAILED_TEST_REV was exported
  # before this script was rerun, and if installrev still points to the
  # same revision number, continue waiting rather than trying to restart.
  # Intervention (such as a build-system reboot) is required to re-test
  # the same version.
  sleep 300
  exec $0
fi
          
          
mailto=/usr/bin/mailto
sendmail=/usr/sbin/sendmail
biabam=/usr/bin/biabam
TUNBR=tunbr
email_failure="zumastor-buildd@google.com"
email_success="zumastor-buildd@google.com"
repo="${top}/zumastor-tests"
export DISKIMG="${top}/zumastor/build/dapper-i386-zumastor-r$installrev.img"
export LOGDIR="${top}/zumastor/build/logs-r$installrev"
[ -d $LOGDIR ] || mkdir $LOGDIR

summary=${LOGDIR}/summary

[ -x /etc/default/testenv ] && . /etc/default/testenv




if [ ! -d ${repo} ] ; then
  echo "svn checkout http://zumastor.googlecode.com/svn/trunk/cbtb/tests zumastor-tests"
  echo "cp $0 to the parent directory of the zumastor-tests repository and "
  echo "run from that location.  Periodically inspect the cbtb/host-scripts"
  echo "for changes and redeploy them."
  exit 1
fi

pushd ${repo}

# build and test the last successfully installed revision
svn update -r $installrev

testret=0

pushd 1
for f in *.sh
do
  # timeout any test that runs for more than an hour
  export LOGPREFIX="$f."
  testlog="${LOGDIR}/${LOGPREFIX}log"
  ${TUNBR} timeout -14 3600 ${top}/test-zuma-dapper-i386.sh $f >${testlog} 2>&1
  testrc=$?
  files="$testlog $files"
  if [ $testrc -eq 0 ]
  then
    echo PASS $f >>$summary
  else
    echo runtests $f testrc=${testrc}
    echo FAIL $f >>$summary
    testret=$testrc
  fi
done
popd

pushd 2
for f in *.sh
do
  export LOGPREFIX="$f."
  testlog="${LOGDIR}/${LOGPREFIX}log"
  ${TUNBR} ${TUNBR} timeout -14 3600 ${top}/test-zuma-dapper-i386.sh $f >${testlog} 2>&1
  testrc=$?
  files="$testlog $files"
  if  [ $testrc -eq 0 ]
  then
    echo PASS $f >>$summary
  else
    testret=$testrc
    echo runtests $f testrc=${testrc}
    echo FAIL $f >>$summary
  fi
done
popd

popd
    
# send summary, logs, and a success or failure subject to the
# success or failure mailing lists
if [ $testret -eq 0 ]; then
  subject="zumastor b$branch r$installrev test success"
  email="${email_success}"

  # creating this symlink only on success will cause the test step to
  # keep repeating on failure.  For the moment, while the continuous build
  # is maturing, this is desired.  Modify the logic later to not loop over
  # test attempts.
  ln -sf $installrev ${top}/testrev

else
  subject="zumastor b$branch r$installrev test failure $testret"
  email="${email_failure}"
  export FAILED_TEST_REV=$installrev
fi


# send $subject and $files to $email
if [ -x ${mailto} ] ; then
  (
    cat $summary
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
    echo "Subject: " $subject
    echo
    cat $summary
    for f in $files
    do
      echo
      echo $f
      echo "------------------"
      cat $f
    done
  ) | ${sendmail} ${email}
fi

rm -f $summary $files

# loop and reload the script
sleep 300
exec $0
