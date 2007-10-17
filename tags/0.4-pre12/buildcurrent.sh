#!/bin/bash -x
#
# $Id$
#
# based on build_packages.sh
# builds whatever is in current working directory,
# does not pull any external source
# may be run inside an isolated virtual machine for safe automatic builds
#
# Copyright 2007 Google Inc. All rights reserved.
# Author: Drake Diedrich (dld@google.com)


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

if [ -f SVNREV ] ; then
  SVNREV=`awk '/^[0-9]+$/ { print $1; }' SVNREV`
else
  SVNREV=`svn info zumastor | grep ^Revision:  | cut -d\  -f2`
fi

SRC=${PWD}
BUILD_DIR=${SRC}/build
LOG=/dev/null
TIME=`date +%s`

[[ -e $1 ]] || { echo "Usage: $0 <path_to_kernel_config> [revision]"; exit 1; }

# [[ $# -eq 2 ]] && REV="-r $2"



[ -d $BUILD_DIR ] || mkdir $BUILD_DIR
cp $1 $BUILD_DIR/$KERNEL_VERSION.config
pushd $BUILD_DIR >> $LOG || exit 1





echo -n Building zumastor Debian package...
pushd ${SRC}/zumastor >> $LOG || exit 1
[ -f debian/changelog ] && rm debian/changelog
EDITOR=/bin/true dch --create --package zumastor -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -uc -us -rfakeroot >> $LOG || exit 1
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.deb ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}
echo -e "done.\n"

echo -n Building ddsnap Debian package...
pushd ${SRC}/ddsnap >> $LOG || exit 1
[ -f debian/changelog ] && rm debian/changelog
EDITOR=/bin/true dch --create --package ddsnap -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -uc -us -rfakeroot >> $LOG || exit 1
make genpatches
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.deb ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}
echo -e "done.\n"

if [ -e linux-${KERNEL_VERSION} ] ; then
  echo Moving old kernel tree to linux-${KERNEL_VERSION}.$TIME; \
  mv linux-${KERNEL_VERSION} linux-${KERNEL_VERSION}.$TIME;
fi

if [ ! -f linux-${KERNEL_VERSION}.tar.bz2 ] ; then
  wget -c http://www.kernel.org/pub/linux/kernel/v2.6/linux-${KERNEL_VERSION}.tar.bz2 >> $LOG || exit $?
fi

echo -n Unpacking kernel...
tar xjf linux-${KERNEL_VERSION}.tar.bz2 || exit 1
echo -e "done.\n"

echo -n "Setting .config ..."
mv $KERNEL_VERSION.config linux-${KERNEL_VERSION}/.config
echo -e "done.\n"

echo Applying patches...
pushd linux-${KERNEL_VERSION} >> $LOG || exit 1

for patch in \
    ${SRC}/zumastor/patches/${KERNEL_VERSION}/* \
    ${SRC}/ddsnap/patches/${KERNEL_VERSION}/*
do
	echo "   $patch"
	< $patch patch -p1 >> $LOG || exit 1
done
echo -e "done.\n"

echo -n Building kernel package...
fakeroot make-kpkg --append_to_version=-zumastor-r$SVNREV --revision=1.0 --initrd  --mkimage="mkinitramfs -o /boot/initrd.img-%s %s" --bzimage kernel_image kernel_headers >> $LOG </dev/null || exit 1
popd >> $LOG
echo -e "done.\n"

popd >> $LOG
echo zumastor packages successfully built in $BUILD_DIR/*.deb
