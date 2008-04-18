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

echo "$0 $*"

buildkernel="true"
kconfig=
while [ $# -ge 1 ]
do
  case $1 in
    --no-kernel)
        buildkernel=false
        ;;
    *)
      kconfig=$1
      ;;
  esac
  shift
done

if [ ! -r "$kconfig" ]
then
  echo "Usage: $0 [--no-kernel] <path_to_kernel_config>"
  exit 1
fi

builduml="false"
if egrep '^CONFIG_UML=y$' "$kconfig"
then
  builduml="true"
fi


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
ARCH=i386



[ -d $BUILD_DIR ] || mkdir $BUILD_DIR
[ -d $BUILD_DIR/r${SVNREV} ] || mkdir $BUILD_DIR/r${SVNREV}
cp $kconfig $BUILD_DIR/$KERNEL_VERSION.config
pushd $BUILD_DIR >> $LOG || exit 1





echo -n Building zumastor Debian package...
pushd ${SRC}/zumastor >> $LOG || exit 1

sed -i -e s,_VER_,${VERSION},g debian/control

echo ${SVNREV} >SVNREV
echo ${VERSION} >SVNVERSION

[ -f debian/changelog ] && rm debian/changelog
EDITOR=/bin/true VISUAL=/bin/true dch --create --package zumastor -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -I.svn -uc -us -rfakeroot >> $LOG || exit 1
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.deb ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}/r${SVNREV}
echo -e "done.\n"

echo -n Building ddsnap Debian package...
pushd ${SRC}/ddsnap >> $LOG || exit 1
echo ${SVNREV} >SVNREV
[ -f debian/changelog ] && rm debian/changelog
EDITOR=/bin/true VISUAL=/bin/true dch --create --package ddsnap -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -I.svn -uc -us -rfakeroot >> $LOG || exit 1
make genpatches
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.deb ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}/r${SVNREV}
echo -e "done.\n"

if [ -e linux-${KERNEL_VERSION} ] ; then
  echo Moving old kernel tree to linux-${KERNEL_VERSION}.$TIME; \
  mv linux-${KERNEL_VERSION} linux-${KERNEL_VERSION}.$TIME;
fi


if [ "$buildkernel" = "true" ]
then
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

  if [ "$builduml" = "true" ]
  then
   echo -n Building UML kernel binary
   make -j4 ARCH=um SUBARCH=i386 linux
   [ -d ../r${SVNREV} ] || mkdir ../r${SVNREV}
   mv linux ../r${SVNREV}/linux-i386-r${SVNREV}
  else
    echo -n Building kernel package...
    fakeroot make-kpkg --append_to_version=-zumastor-r$SVNREV --revision=1.0 --initrd  --mkimage="mkinitramfs -o /boot/initrd.img-%s %s" --bzimage kernel_image kernel_headers >> $LOG </dev/null || exit 1
    for kfile in \
      kernel-image-${KERNEL_VERSION}-zumastor-r${SVNREV}_1.0_${ARCH}.deb \
      kernel-headers-${KERNEL_VERSION}-zumastor-r${SVNREV}_1.0_${ARCH}.deb
    do
      [ -f ../$kfile ] && mv ../$kfile ${BUILD_DIR}/r${SVNREV}/
    done
  fi
  popd >> $LOG
  echo -e "done.\n"
fi

popd >> $LOG
