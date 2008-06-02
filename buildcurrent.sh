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
	kconfig=/dev/null
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

# Get the svn revision number from the file REVISION, svnversion, or by scraping
# the output of svn log, in order until one is successful
REVISION=`awk '/^[0-9]+$/ { print $1; }' REVISION 2>/dev/null || svnversion | tr [A-Z] [a-z] || svn info zumastor | grep ^Revision:  | cut -d\  -f2`
if echo "x$REVISION" | grep -q ':'
then
  echo "Split repository.  Go to the top of the tree and do 'svn update'!"
  exit 1
fi
if [ "x$REVISION" = "x" ]
then
  REVISION=unknown0
elif [ "x$REVISION" = "xexported" ]
then
  REVISION=exported0
fi
SRC=${PWD}
BUILD_DIR=${SRC}/build
LOG=/dev/null
TIME=`date +%s`
ARCH="$ARCH"
if [ -z "$ARCH" ]
then
  if [ `uname -m || echo i386` = "x86_64" ]
  then
    ARCH=amd64
  else
    ARCH=i386
  fi
fi
HOSTARCH=$ARCH


[ -d $BUILD_DIR ] || mkdir $BUILD_DIR
[ -d $BUILD_DIR/r${REVISION} ] || mkdir $BUILD_DIR/r${REVISION}
cp $kconfig $BUILD_DIR/$KERNEL_VERSION.config
pushd $BUILD_DIR >> $LOG || exit 1

VERSION_STRING="${VERSION}-r${REVISION}"

echo -n Building zumastor Debian package...
pushd ${SRC}/zumastor >> $LOG || exit 1

echo ${VERSION_STRING} > VERSION_STRING
[ -f debian/changelog ] && rm debian/changelog
EDITOR=/bin/true VISUAL=/bin/true dch --create --package zumastor -u low --no-query -v $VERSION_STRING "revision $REVISION" || exit 1
dpkg-buildpackage -I.svn -uc -us -rfakeroot >> $LOG || exit 1
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.deb ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}/r${REVISION}
echo -e "done.\n"

echo -n Building ddsnap Debian package...
pushd ${SRC}/ddsnap >> $LOG || exit 1
echo ${VERSION_STRING} > VERSION_STRING
[ -f debian/changelog ] && rm debian/changelog
EDITOR=/bin/true VISUAL=/bin/true dch --create --package ddsnap -u low --no-query -v $VERSION_STRING "revision $REVISION" || exit 1
dpkg-buildpackage -I.svn -uc -us -rfakeroot >> $LOG || exit 1
make genpatches
popd >> $LOG
mv ${SRC}/*.changes ${SRC}/*.deb ${SRC}/*.tar.gz ${SRC}/*.dsc ${BUILD_DIR}/r${REVISION}
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
   if [ "$HOSTARCH" = "amd64" ]
   then
     SUBARCH=x86_64
   else
     SUBARCH=i386
   fi
   make -j4 ARCH=um SUBARCH=$SUBARCH linux
   [ -d ../r${REVISION} ] || mkdir ../r${REVISION}
   mv linux ../r${REVISION}/linux-${HOSTARCH}-r${REVISION}
  else
    echo -n Building kernel package...
    fakeroot make-kpkg --append_to_version=-zumastor-r$REVISION --revision=1.0 --initrd  --mkimage="mkinitramfs -o /boot/initrd.img-%s %s" --bzimage kernel_image kernel_headers >> $LOG </dev/null || exit 1
    for kfile in \
      kernel-image-${KERNEL_VERSION}-zumastor-r${REVISION}_1.0_${HOSTARCH}.deb \
      kernel-headers-${KERNEL_VERSION}-zumastor-r${REVISION}_1.0_${HOSTARCH}.deb
    do
      [ -f ../$kfile ] && mv ../$kfile ${BUILD_DIR}/r${REVISION}/
    done
  fi
  popd >> $LOG
  echo -e "done.\n"
fi

popd >> $LOG
