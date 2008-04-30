#!/bin/bash -x

# build packages and UML test kernel natively on Debian/etch i386

set -e

DIST=etch

# Currently, if you use this on hardy or above, it will break with libc fun
# times. Please use something less modern for now [dapper or gutsy?]
. /etc/lsb-release
if [ "x$DISTRIB_CODENAME" = "xhardy" ]
then
  echo "Failing because your OS is too new. This script will fail on hardy"
  exit 8
fi

ARCH=i386

# Get the directory paths (grandparent)
OLDPWD=$PWD
cd ../..
SRC=${PWD}
BUILD_DIR=${SRC}/build
if [ ! -d $BUILD_DIR ]
then
  mkdir -p $BUILD_DIR
fi
cd $OLDPWD


# Cache the prepared  userspace.  Runs once.
ext3=$BUILD_DIR/$DIST-$ARCH.ext3

if [ ! -e $ext3 ]
then
  ./debootstrap-$DIST-$ARCH.sh
fi

# Get the versions of the kernel and repository.
OLDPWD=$PWD
cd ${SRC}
KERNEL_VERSION=`awk '/^2\.6\.[0-9]+(\.[0-9]+)?$/ { print $1; }' KernelVersion`
if [ "x$KERNEL_VERSION" = "x" ] ; then
  echo "Suspect KernelVersion file"
  exit 1
fi

VERSION=`awk '/[0-9]+\.[0-9]+(\.[0-9]+)?$/ { print $1; }' Version`
if [ "x$VERSION" = "x" ] ; then
  echo "Suspect Version file"
  exit 1
fi

SVNREV=`awk '/^[0-9]+$/ { print $1; }' SVNREV || svnversion | tr [A-Z] [a-z] || svn info zumastor | grep ^Revision:  | cut -d\  -f2`


# Build the userspace debs and the UML kernel
./buildcurrent.sh kernel/config/$KERNEL_VERSION-um-uml

# Unpack the userspace into a fresh, sparse filesystem
uda=`mktemp`
rootdir=`mktemp -d`
cp --sparse=always $ext3 $uda
sudo mount -oloop,rw $uda $rootdir

currentbuilddir=$BUILD_DIR/r${SVNREV}
zumastorpkg=$currentbuilddir/zumastor_$VERSION-r${SVNREV}_all.deb
ddsnappkg=$currentbuilddir/ddsnap_$VERSION-r${SVNREV}_$ARCH.deb
if [ ! -e $zumastorpkg ]
then
  echo "The zumastor package was not built!"
  exit 1
fi
if [ ! -e $ddsnappkg ]
then
  echo "The ddsnap package was not built!"
  exit 1
fi

# install the new zumastor userspace programs
cp $zumastorpkg $ddsnappkg $rootdir/tmp

sudo chroot $rootdir dpkg -i /tmp/ddsnap_$VERSION-r${SVNREV}_$ARCH.deb \
  /tmp/zumastor_$VERSION-r${SVNREV}_all.deb
sudo rm $rootdir/tmp/*.deb
sudo umount $rootdir
rmdir $rootdir

mv $uda $BUILD_DIR/r$SVNREV/$DIST-$ARCH-zumastor-r$SVNREV.ext3

cd $OLDPWD
