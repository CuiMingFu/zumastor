#!/bin/bash -x
# Build a 2.6.24 kernel package for Zumastor

VERSION="2.6.24-12.22"
ZUMAREVISION=`svn info ../../ | grep ^Revision:  | cut -d\  -f2`
PKGVERSION="$VERSION~zumappa$ZUMAREVISION"
MIRROR="http://mirrors.kernel.org/ubuntu"

WORKDIR=`mktemp -d`

wget $MIRROR/pool/main/l/linux/linux_2.6.24.orig.tar.gz -O \
  $WORKDIR/linux_2.6.24.orig.tar.gz
wget $MIRROR/pool/main/l/linux/linux_2.6.24-12.22.dsc -O \
  $WORKDIR/linux_2.6.24-12.22.dsc
wget $MIRROR/pool/main/l/linux/linux_2.6.24-12.22.diff.gz -O \
  $WORKDIR/linux_2.6.24-12.22.diff.gz

CWD=`pwd`
cd $WORKDIR
dpkg-source -x linux_2.6.24-12.22.dsc
cd linux-2.6.24
for patchfile in $CWD/patches/*.patch
do
  echo "Using Patch $patchfile"
  patch -p5 < $patchfile
done
mkdir debian/binary-custom.d/zumastor debian/binary-custom.d/zumastor/patchset
cp $CWD/../config/2.6.24.2-i386-full debian/binary-custom.d/zumastor/config.i386
echo "CONFIG_VERSION_SIGNATURE=\"Ubuntu 2.6.24-4.6-zumastor\"" >> debian/binary-custom.d/zumastor/config.i386
cp $CWD/../config/2.6.24.2-amd64-full debian/binary-custom.d/zumastor/config.amd64
echo "CONFIG_VERSION_SIGNATURE=\"Ubuntu 2.6.24-4.6-zumastor\"" >> debian/binary-custom.d/zumastor/config.amd64
patchnum=0
for patch in $CWD/../../ddsnap/patches/2.6.24.2/*
do
  cp $patch debian/binary-custom.d/zumastor/patchset/00${patchnum}-`basename $patch`
  patchnum=$((${patchnum} + 1))
done

mkpatchdir=`mktemp -d`
autopatch=`mktemp`
mkdir -p $mkpatchdir/kernel-orig/drivers/md
mkdir -p $mkpatchdir/kernel-zumastor/drivers/md
cp $CWD/../../ddsnap/kernel/dm-ddsnap.* $mkpatchdir/kernel-zumastor/drivers/md
cd $mkpatchdir
diff -ruNp kernel-orig kernel-zumastor > $autopatch
cd $WORKDIR/linux-2.6.24
cp $autopatch debian/binary-custom.d/zumastor/patchset/00${patchnum}-AUTOdm.patch
rm -rf $mkpatchdir
rm $autopatch

echo "# Placeholder" > debian/binary-custom.d/zumastor/rules

DEBEMAIL="Zumastor Builder <zuambuild@gmail.com>" dch -v $PKGVERSION -b
dpkg-buildpackage -rfakeroot -S
echo "Results in $WORKDIR"
