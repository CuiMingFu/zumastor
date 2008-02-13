#!/bin/bash -x
#
# $Id$
#
# based on buildcurrent.sh
# builds whatever is in current working directory,
# does not pull any external source
# may be run inside an isolated virtual machine for safe automatic builds
# Only builds deb src packages for upload to builder
#
# Copyright 2007 Google Inc. All rights reserved.
# Author: Will Nowak (willn@google.com)

echo "$0 $*"

KERNEL_VERSION=`awk '/^2\.6\.[0-9]+(\.[0-9]+)?$/ { print $1; }' KernelVersion`
if [ "x$KERNEL_VERSION" = "x" ] ; then
  echo "Suspect KernelVersion file"
  exit 1
fi

VERSION=`awk '/^[0-9]+\.[0-9]+(\.[0-9]+)?$/ { print $1; }' Version`
if [ "x$VERSION" = "x" ] ; then
  echo "Suspect Version file"
  exit 1
fi

# Get the svn revision number from the file SVNREV, svnversion, or by scraping
# the output of svn log, in order until one is successful
SVNREV=`awk '/^[0-9]+$/ { print $1; }' SVNREV || svnversion | tr [A-Z] [a-z] || svn info zumastor | grep ^Revision:  | cut -d\  -f2`

SRC=${PWD}
BUILD_DIR=${SRC}/build
LOG=/dev/null
TIME=`date +%s`
DIST="gutsy"


[ -d $BUILD_DIR ] || mkdir $BUILD_DIR
[ -d $BUILD_DIR/r${SVNREV} ] || mkdir $BUILD_DIR/r${SVNREV}

echo -n Building zumastor Debian package...
pushd ${SRC}/zumastor >> $LOG || exit 1
echo ${SVNREV} >SVNREV

export EMAIL="zuambuild@gmail.com"
export VISUAL=/bin/true
export EDITOR=/bin/true
export NAME="Zumastor Builder"
[ -f debian/changelog.template ] && cp -f debian/changelog
dch -u low -D $DIST --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -S -I.svn -uc -us -rfakeroot >> $LOG || exit 1
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}/r${SVNREV}
echo -e "done.\n"

echo -n Building ddsnap Debian package...
pushd ${SRC}/ddsnap >> $LOG || exit 1
echo ${SVNREV} >SVNREV
[ -f debian/changelog.template ] && cp -f debian/changelog
dch -u low --no-query -D $DIST -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -S -I.svn -uc -us -rfakeroot >> $LOG || exit 1
make genpatches
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}/r${SVNREV}
echo -e "done.\n"

for changes in ${BUILD_DIR}/r${SVNREV}/*.changes
do
  debsign $changes
  dput zumastor $changes
done
