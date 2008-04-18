#!/bin/sh -x

set -e

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


# Cache the prepared dapper userspace.  Runs once.
dapperext3=$BUILD_DIR/dapper-i386.ext3
if [ ! -e $dapperext3 ]
then
  ./debootstrap-dapper-i386.sh
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

ARCH=i386

# Build the userspace debs and the UML kernel
./buildcurrent.sh kernel/config/$KERNEL_VERSION-um-uml

# Unpack the userspace into a fresh, sparse filesystem
uda=`mktemp`
rootdir=`mktemp -d`
cp --sparse=always $dapperext3 $uda
sudo mount -oloop,rw $uda $rootdir

# install the new zumastor userspace programs
cp $BUILD_DIR/zumastor_$VERSION-r${SVNREV}_all.deb \
  $BUILD_DIR/ddsnap_$VERSION-r${SVNREV}_$ARCH.deb \
  $rootdir/tmp

sudo chroot $rootdir dpkg -i /tmp/ddsnap_$VERSION-r${SVNREV}_$ARCH.deb \
  /tmp/zumastor_$VERSION-r${SVNREV}_all.deb
sudo rm $rootdir/tmp/*.deb
sudo umount $rootdir
rmdir $rootdir

mv $uda $BUILD_DIR/dapper-i386-zumastor-r$SVNREV.ext3

cd $OLDPWD
