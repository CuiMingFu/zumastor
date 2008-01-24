#!/bin/sh -x

# build packages and UML test kernel natively on Debian/etch i386

set -e

DIST=etch
ARCH=i386

# Get the directory paths (grandparent)
pushd ../..
SRC=${PWD}
BUILD_DIR=${SRC}/build
if [ ! -d $BUILD_DIR ]
then
  mkdir -p $BUILD_DIR
fi
popd


# Cache the prepared  userspace.  Runs once.
ext3=$BUILD_DIR/$DIST-$ARCH.ext3

if [ ! -e $ext3 ]
then
  ./debootstrap-$DIST-$ARCH.sh
fi

# Get the versions of the kernel and repository.
pushd ${SRC}
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

# install the new zumastor userspace programs
cp $BUILD_DIR/r${SVNREV}/zumastor_$VERSION-r${SVNREV}_$ARCH.deb \
  $BUILD_DIR/r${SVNREV}/ddsnap_$VERSION-r${SVNREV}_$ARCH.deb \
  $rootdir/tmp

sudo chroot $rootdir dpkg -i /tmp/ddsnap_$VERSION-r${SVNREV}_$ARCH.deb \
  /tmp/zumastor_$VERSION-r${SVNREV}_$ARCH.deb
sudo rm $rootdir/tmp/*.deb
sudo umount $rootdir
rmdir $rootdir

mv $uda $BUILD_DIR/r$SVNREV/$DIST-$ARCH-zumastor-r$SVNREV.ext3

popd
