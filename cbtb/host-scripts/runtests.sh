#!/bin/sh -x
#
# $Id$
#
# Run the full test cycle once.  May be updated, changing the behavior
# of the continuous build between versions.
#
# Copyright 2007 Google Inc.  All rights reserved.
# Author: Drake Diedrich (dld@google.com)
# License: GPLv2
#

set -e

repo=${PWD}
diskimgdir=${HOME}/testenv
[ -x /etc/default/testenv ] && . /etc/default/testenv

TUNBR=tunbr



pushd cbtb/tests/1
for f in *-test.sh
do
  # timeout any test that runs for more than an hour
  if time ${TUNBR} timeout -14 3600 ../../../../test-zuma-dapper-i386.sh $f
  then
    echo PASS $f
  else
    retval=$?
    echo FAIL $f
  fi
done
popd

pushd cbtb/tests/2
for f in *-test.sh
do
  if ${TUNBR} ${TUNBR} timeout -14 3600 ../../../../test-zuma-dapper-i386.sh $f
  then
    echo PASS $f
  else
    retval=$?
    echo FAIL $f
  fi
done
popd

echo ${retval}
exit ${retval}